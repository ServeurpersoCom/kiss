import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Assistant, Grant, Message, Round, ToolContext, Verdict } from '../src/lib/types.js';
import { ONCE } from '../src/lib/types.js';
import { pulse } from '../src/lib/pulse.js';
import { STOPPED } from '../src/lib/conversation.js';

interface Body {
	tools: { function: { name: string } }[];
	messages: { role: string; content?: string; reasoning_content?: string; tool_calls?: object[] }[];
}

// the answer to one request, given the signal of that request
type Reply = (signal: AbortSignal) => Response;

// one streamed answer: closed by [DONE], cut short, or left open; like the body
// of a real fetch, an open one fails once its request aborts
function stream(deltas: object[], end: 'done' | 'cut' | 'open' = 'done'): Reply {
	const events = deltas.map((d) => `data: ${JSON.stringify({ choices: [{ delta: d }] })}\n\n`);
	const text = events.join('') + (end === 'done' ? 'data: [DONE]\n\n' : '');
	return (signal) =>
		new Response(
			new ReadableStream({
				start(c) {
					c.enqueue(new TextEncoder().encode(text));
					if (end === 'open') signal.addEventListener('abort', () => c.error(signal.reason));
					else c.close();
				}
			}),
			{ headers: { 'content-type': 'text/event-stream' } }
		);
}

function calls(...lines: [string, string][]): object {
	return {
		tool_calls: lines.map(([id, l], index) => ({
			index,
			id,
			function: { name: 'config', arguments: JSON.stringify({ lines: l }) }
		}))
	};
}

// a page talking to a model that answers each request with the next reply
async function page(replies: Reply[]) {
	vi.resetModules();
	const engine = await import('../src/engine/run.js');
	engine.start();
	await engine.run('set endpoints url m http://m/v1\nset chat model m/x', 'user');
	const bodies: Body[] = [];
	vi.stubGlobal(
		'fetch',
		vi.fn(async (_url: string, init: RequestInit) => {
			bodies.push(JSON.parse(String(init.body)));
			return replies.shift()!(init.signal!);
		})
	);
	const { turn } = await import('../src/lib/agent.js');
	const { settled } = await import('../src/lib/conversation.js');
	const stop = new AbortController();
	// what the model asked the user, answered by the verdicts in order, once past them
	const asked: Grant[] = [];
	const verdicts: Verdict[] = [];
	const grant = async (request: Grant) => (asked.push(request), verdicts.shift() ?? ONCE);
	const reply: Assistant = { role: 'assistant', rounds: [] };
	// the rounds as the page last stored them
	const kept: { rounds: Round[] } = { rounds: [] };
	const tools: ToolContext = {
		signal: stop.signal,
		cli: (l) => engine.run(l, 'llm', { signal: stop.signal, grant }),
		redact: engine.redact,
		grant,
		keep: async () => void (kept.rounds = settled(reply.rounds))
	};
	const user: Message[] = [{ role: 'user', text: 'go' }];
	return {
		engine,
		bodies,
		stop,
		tools,
		reply,
		asked,
		verdicts,
		settled,
		kept,
		go: () => turn(user, reply, tools, stop.signal, pulse(performance.now()))
	};
}

beforeEach(() => {
	localStorage.clear();
	vi.unstubAllGlobals();
});

describe('a turn', () => {
	it('asks the tokens counted, and keeps what it spent, the count of the endpoint first', async () => {
		// the count of the endpoint on a last chunk of its own, before [DONE]
		const usage: Reply = () => {
			const chunks = [
				...['a', 'b', 'c'].map((content) => ({ choices: [{ delta: { content } }] })),
				{ choices: [], usage: { completion_tokens: 7 } }
			];
			const text =
				chunks.map((c) => `data: ${JSON.stringify(c)}\n\n`).join('') + 'data: [DONE]\n\n';
			return new Response(text, { headers: { 'content-type': 'text/event-stream' } });
		};
		const p = await page([usage]);
		await p.go();
		expect(p.bodies[0]).toMatchObject({ stream: true, stream_options: { include_usage: true } });
		expect(p.reply.stats).toMatchObject({ tokens: 7 });
		expect(p.reply.rounds[0].text).toBe('abc');
	});

	it('counts one token per chunk when the endpoint gives no count', async () => {
		const p = await page([
			stream([{ content: 'a' }, { reasoning_content: 'r' }, { content: 'b' }])
		]);
		await p.go();
		expect(p.reply.stats).toMatchObject({ tokens: 3 });
	});

	it('sends the parameters set for its model only, numbers as numbers', async () => {
		const p = await page([stream([{ content: 'ok' }])]);
		await p.engine.run(
			'set models temperature m/x 0.5\nset models reasoning_effort m/x high\nset models top_k m/y 3',
			'user'
		);
		await p.go();
		expect(p.bodies[0]).toMatchObject({ temperature: 0.5, reasoning_effort: 'high' });
		expect(p.bodies[0]).not.toHaveProperty('top_k');
	});

	it('reads the configuration as it stands every round', async () => {
		const p = await page([
			stream([calls(['a', 'set models temperature m/x 0.3\nset tools rounds 2'])]),
			stream([calls(['b', 'set tools use config off'])]),
			stream([calls(['c', 'show version'])])
		]);
		await p.engine.run('set tools rounds 3', 'user');
		await p.go();
		expect(p.bodies[0]).not.toHaveProperty('temperature');
		expect(p.bodies[1]).toMatchObject({ temperature: 0.3 });
		expect(p.bodies).toHaveLength(2);
		expect(p.reply.error).toBe('stopped after 2 tool rounds');
	});

	it('hides a tool turned off from the next round, and refuses its calls', async () => {
		const p = await page([
			stream([calls(['a', 'set tools use config off'])]),
			stream([calls(['b', 'show version'])]),
			stream([{ content: 'done' }])
		]);
		await p.go();
		const names = (i: number) => p.bodies[i].tools.map((t) => t.function.name);
		expect(names(0)).toContain('config');
		expect(names(1)).not.toContain('config');
		expect(p.reply.rounds[1].calls[0]).toMatchObject({
			result: 'unknown tool "config"',
			ok: false
		});
	});

	it('stops after the rounds set', async () => {
		const p = await page([stream([calls(['a', 'show version'])])]);
		await p.engine.run('set tools rounds 1', 'user');
		await p.go();
		expect(p.bodies).toHaveLength(1);
		expect(p.reply.error).toBe('stopped after 1 tool rounds');
	});

	it('sends every round as one assistant message and its results', async () => {
		const p = await page([
			stream([calls(['a', 'show version'])]),
			stream([calls(['b', 'show display thinking'])]),
			stream([{ content: 'done' }])
		]);
		await p.go();
		const roles = p.bodies[2].messages.map((m) => m.role + (m.tool_calls ? '+calls' : ''));
		expect(roles).toEqual(['user', 'assistant+calls', 'tool', 'assistant+calls', 'tool']);
		expect(p.reply.rounds.map((r) => r.calls.map((c) => c.id))).toEqual([['a'], ['b'], []]);
	});

	it('sends the reasoning back with its round', async () => {
		const p = await page([
			stream([{ reasoning_content: 'think' }, calls(['a', 'show version']), { content: 'after' }]),
			stream([{ content: 'done' }])
		]);
		await p.go();
		expect(p.bodies[1].messages[1]).toMatchObject({
			role: 'assistant',
			content: 'after',
			reasoning_content: 'think'
		});
	});

	it('runs no call after a stop, and keeps the calls sent before it, the one it cut as stopped', async () => {
		const p = await page([
			stream([
				calls(
					['a', 'show version'],
					['b', 'show display thinking'],
					['c', 'set display thinking open']
				)
			])
		]);
		const cli = vi.fn(async (l: string) => {
			if (cli.mock.calls.length === 2) p.stop.abort();
			return { ok: true, text: l };
		});
		p.tools.cli = cli;
		await expect(p.go()).rejects.toThrow();
		expect(cli).toHaveBeenCalledTimes(2);
		expect(p.reply.rounds[0].calls).toMatchObject([
			{ id: 'a', ok: true },
			{ id: 'b', args: '{"lines":"show display thinking"}', ok: false, result: STOPPED }
		]);
		expect(p.engine.settings.get('display thinking')).toBe('closed');
	});

	it('stores no secret the model writes, from before its call is sent, even in arguments that read as no object', async () => {
		const config = (id: string, args: string) => ({
			tool_calls: [{ index: 0, id, function: { name: 'config', arguments: args } }]
		});
		const p = await page([
			stream([calls(['a', 'set endpoints key m sk-model'])]),
			stream([config('b', '{"lines":"set endpoints key m sk-broken')]),
			stream([
				config('c', JSON.stringify(JSON.stringify({ lines: 'set endpoints key m sk-dbl' })))
			]),
			stream([{ content: 'done' }])
		]);
		await p.go();
		expect(p.kept.rounds[0].calls[0].args).toBe('{"lines":"set endpoints key m <removed>"}');
		for (const r of [1, 2])
			expect(p.reply.rounds[r].calls[0]).toMatchObject({ args: '{}', ok: false });
		expect(JSON.stringify(p.reply.rounds)).not.toMatch(/sk-model|sk-broken|sk-dbl/);
	});

	it('settles, even mid stream, to what streamed and the calls that ran', async () => {
		const p = await page([stream([{ content: 'hi' }, calls(['a', 'set display thi'])], 'open')]);
		const turn = p.go();
		await vi.waitFor(() => expect(p.reply.rounds[0]?.calls.length).toBe(1));
		expect(p.settled(p.reply.rounds)).toEqual([{ reasoning: '', text: 'hi', calls: [] }]);
		p.stop.abort();
		await expect(turn).rejects.toThrow();
	});

	it('runs nothing from a stream closed before [DONE]', async () => {
		const p = await page([stream([calls(['a', 'set display thinking open'])], 'cut')]);
		await expect(p.go()).rejects.toThrow('ended before [DONE]');
		expect(p.reply.rounds).toEqual([]);
		expect(p.engine.settings.get('display thinking')).toBe('closed');
	});
});
