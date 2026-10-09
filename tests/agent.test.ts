import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Assistant, Grant, Message, ToolContext, Verdict } from '../src/lib/types.js';
import { ONCE } from '../src/lib/types.js';

interface Body {
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
	const tools: ToolContext = {
		signal: stop.signal,
		cli: (l) => engine.run(l, 'llm', { signal: stop.signal, grant }),
		redact: engine.redact,
		grant
	};
	const reply: Assistant = { role: 'assistant', rounds: [] };
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
		go: () => turn(user, reply, tools, stop.signal)
	};
}

beforeEach(() => {
	localStorage.clear();
	vi.unstubAllGlobals();
});

describe('a turn', () => {
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

	it('runs no call after a stop, and keeps the calls that ended before it', async () => {
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
		expect(p.reply.rounds[0].calls.map((c) => c.id)).toEqual(['a']);
		expect(p.engine.settings.get('display thinking')).toBe('closed');
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
