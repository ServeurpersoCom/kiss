import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Assistant, Message, ToolContext } from '../src/lib/types.js';

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
	const { settled, turn } = await import('../src/lib/agent.js');
	const stop = new AbortController();
	const tools: ToolContext = {
		signal: stop.signal,
		cli: (l) => engine.run(l, 'llm', stop.signal),
		redact: engine.redact
	};
	const reply: Assistant = { role: 'assistant', rounds: [] };
	const user: Message[] = [{ role: 'user', text: 'go' }];
	return {
		engine,
		bodies,
		stop,
		tools,
		reply,
		settled,
		go: () => turn(user, reply, tools, stop.signal)
	};
}

beforeEach(() => {
	localStorage.clear();
	vi.unstubAllGlobals();
});

describe('a turn', () => {
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
