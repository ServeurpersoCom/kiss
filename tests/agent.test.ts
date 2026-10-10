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

// one streamed answer of named events, the event that ends it included
function events(list: object[]): Reply {
	const text = list
		.map((e) => `event: ${(e as { type: string }).type}\ndata: ${JSON.stringify(e)}\n\n`)
		.join('');
	return () => new Response(text, { headers: { 'content-type': 'text/event-stream' } });
}

// a page talking to a model that answers each request with the next reply,
// configured by the lines given after its endpoint and model
async function page(replies: Reply[], lines = '') {
	vi.resetModules();
	const engine = await import('../src/engine/run.js');
	engine.start();
	await engine.run(`set endpoints url m http://m/v1\nset chat model m/x\n${lines}`, 'user');
	const bodies: Body[] = [];
	const requests: { url: string; headers: Record<string, string> }[] = [];
	vi.stubGlobal(
		'fetch',
		vi.fn(async (url: string, init: RequestInit) => {
			bodies.push(JSON.parse(String(init.body)));
			requests.push({ url, headers: init.headers as Record<string, string> });
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
		requests,
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

describe('the messages protocol', () => {
	const MESSAGES = 'set endpoints protocol m messages\nset models max_tokens m/x 1000';
	const block = (index: number, content_block: object) => ({
		type: 'content_block_start',
		index,
		content_block
	});
	const delta = (index: number, d: object) => ({ type: 'content_block_delta', index, delta: d });
	const stop = (index: number) => ({ type: 'content_block_stop', index });
	const answer = (list: object[]) => events([...list, { type: 'message_stop' }]);

	it('carries the key in its own header, the thinking summarized, the prompt cached, every tool streaming its input', async () => {
		const p = await page(
			[
				answer([block(0, { type: 'text', text: '' }), delta(0, { type: 'text_delta', text: 'ok' })])
			],
			`${MESSAGES}\nset endpoints key m sk-a\nset chat system be brief\nset models seed m/x 7`
		);
		await p.go();
		expect(p.requests[0].url).toBe('http://m/v1/messages');
		expect(p.requests[0].headers).toMatchObject({ 'x-api-key': 'sk-a' });
		expect(p.requests[0].headers).not.toHaveProperty('Authorization');
		const body = p.bodies[0] as unknown as Record<string, unknown>;
		expect(body).toMatchObject({
			max_tokens: 1000,
			system: 'be brief',
			thinking: { type: 'adaptive', display: 'summarized' },
			cache_control: { type: 'ephemeral' },
			tools: [{ name: 'config', eager_input_streaming: true }]
		});
		expect(body).not.toHaveProperty('seed');
		expect(p.reply.rounds[0].text).toBe('ok');
	});

	it('streams thinking, text and calls by block, and sends the thinking back signed before them, their results in the user turn after', async () => {
		const p = await page(
			[
				answer([
					block(0, { type: 'thinking', thinking: '', signature: '' }),
					delta(0, { type: 'thinking_delta', thinking: 'thi' }),
					delta(0, { type: 'thinking_delta', thinking: 'nk' }),
					delta(0, { type: 'signature_delta', signature: 'sig' }),
					stop(0),
					block(1, { type: 'text', text: '' }),
					delta(1, { type: 'text_delta', text: 'after' }),
					stop(1),
					block(2, { type: 'tool_use', id: 'a', name: 'config', input: {} }),
					delta(2, { type: 'input_json_delta', partial_json: '{"lines":' }),
					delta(2, { type: 'input_json_delta', partial_json: '"show version"}' }),
					stop(2),
					{ type: 'message_delta', delta: { stop_reason: 'tool_use' }, usage: { output_tokens: 9 } }
				]),
				answer([
					block(0, { type: 'text', text: '' }),
					delta(0, { type: 'text_delta', text: 'done' })
				])
			],
			MESSAGES
		);
		await p.go();
		const thinking = { type: 'thinking', thinking: 'think', signature: 'sig' };
		expect(p.reply.rounds[0]).toMatchObject({
			reasoning: 'think',
			text: 'after',
			calls: [{ id: 'a', name: 'config', args: '{"lines":"show version"}', ok: true }],
			opaque: { protocol: 'messages', items: [thinking] }
		});
		expect((p.bodies[1] as unknown as { messages: object[] }).messages).toEqual([
			{ role: 'user', content: [{ type: 'text', text: 'go' }] },
			{
				role: 'assistant',
				content: [
					thinking,
					{ type: 'text', text: 'after' },
					{ type: 'tool_use', id: 'a', name: 'config', input: { lines: 'show version' } }
				]
			},
			{
				role: 'user',
				content: [{ type: 'tool_result', tool_use_id: 'a', content: expect.any(String) }]
			}
		]);
	});

	it('sends no thinking a round kept for another protocol', async () => {
		const p = await page([answer([])], MESSAGES);
		const reply: Message = {
			role: 'assistant',
			rounds: [
				{
					reasoning: 'r',
					text: 't',
					calls: [],
					opaque: { protocol: 'chat', items: [{ type: 'thinking' }] }
				}
			]
		};
		const { turn } = await import('../src/lib/agent.js');
		const history: Message[] = [{ role: 'user', text: 'a' }, reply, { role: 'user', text: 'b' }];
		const next: Assistant = { role: 'assistant', rounds: [] };
		await turn(history, next, p.tools, p.stop.signal, pulse(performance.now()));
		expect((p.bodies[0] as unknown as { messages: object[] }).messages[1]).toEqual({
			role: 'assistant',
			content: [{ type: 'text', text: 't' }]
		});
	});

	it('needs the tokens a reply may take', async () => {
		const p = await page([], 'set endpoints protocol m messages');
		await expect(p.go()).rejects.toThrow('messages needs models max_tokens');
		expect(p.bodies).toHaveLength(0);
	});
});

describe('the responses protocol', () => {
	const RESPONSES = 'set endpoints protocol m responses';
	const added = (output_index: number, item: object) => ({
		type: 'response.output_item.added',
		output_index,
		item
	});
	const done = (output_index: number, item: object) => ({
		type: 'response.output_item.done',
		output_index,
		item
	});
	const completed = (output_tokens: number) => ({
		type: 'response.completed',
		response: { usage: { output_tokens } }
	});
	const text = (delta: string) => ({ type: 'response.output_text.delta', delta });

	it('sends the system as instructions, nothing stored, the summary of the reasoning asked, every tool as it is', async () => {
		const p = await page(
			[events([text('ok'), completed(1)])],
			`${RESPONSES}\nset endpoints key m sk-a\nset chat system be brief\nset models max_tokens m/x 500\nset models reasoning_effort m/x high\nset models top_k m/x 3`
		);
		await p.go();
		expect(p.requests[0].url).toBe('http://m/v1/responses');
		expect(p.requests[0].headers).toMatchObject({ Authorization: 'Bearer sk-a' });
		const body = p.bodies[0] as unknown as Record<string, unknown>;
		expect(body).toMatchObject({
			instructions: 'be brief',
			input: [{ role: 'user', content: 'go' }],
			max_output_tokens: 500,
			reasoning: { effort: 'high', summary: 'auto' },
			store: false,
			tools: [{ type: 'function', name: 'config', strict: false }]
		});
		expect(body).not.toHaveProperty('top_k');
		expect(p.reply.rounds[0].text).toBe('ok');
		expect(p.reply.stats).toMatchObject({ tokens: 1 });
	});

	it('streams the summary, the text and the calls by item, and sends the reasoning back as it came before them, their outputs after', async () => {
		const reasoning = {
			type: 'reasoning',
			id: 'rs_1',
			summary: [{ type: 'summary_text', text: 'one' }],
			encrypted_content: 'enc'
		};
		const p = await page(
			[
				events([
					added(0, { type: 'reasoning', id: 'rs_1', summary: [] }),
					{ type: 'response.reasoning_summary_part.added', output_index: 0, summary_index: 0 },
					{ type: 'response.reasoning_summary_text.delta', output_index: 0, delta: 'one' },
					{ type: 'response.reasoning_summary_part.added', output_index: 0, summary_index: 1 },
					{ type: 'response.reasoning_summary_text.delta', output_index: 0, delta: 'two' },
					done(0, reasoning),
					added(1, { type: 'message', id: 'msg_1', phase: 'commentary' }),
					text('after'),
					done(1, { type: 'message', id: 'msg_1', phase: 'commentary', content: [] }),
					added(2, { type: 'function_call', id: 'fc_1', call_id: 'a', name: 'config' }),
					{ type: 'response.function_call_arguments.delta', output_index: 2, delta: '{"lines":' },
					{
						type: 'response.function_call_arguments.delta',
						output_index: 2,
						delta: '"show version"}'
					},
					completed(9)
				]),
				events([text('done'), completed(1)])
			],
			RESPONSES
		);
		await p.go();
		expect(p.reply.rounds[0]).toMatchObject({
			reasoning: 'one\n\ntwo',
			text: 'after',
			calls: [{ id: 'a', name: 'config', args: '{"lines":"show version"}', ok: true }],
			opaque: {
				protocol: 'responses',
				items: [reasoning, { type: 'message', phase: 'commentary' }]
			}
		});
		expect((p.bodies[1] as unknown as { input: object[] }).input).toEqual([
			{ role: 'user', content: 'go' },
			reasoning,
			{ role: 'assistant', content: 'after', phase: 'commentary' },
			{
				type: 'function_call',
				call_id: 'a',
				name: 'config',
				arguments: '{"lines":"show version"}'
			},
			{ type: 'function_call_output', call_id: 'a', output: expect.any(String) }
		]);
		expect(p.reply.stats).toMatchObject({ tokens: 10 });
	});

	it('sends no reasoning with a round that has nothing to follow it', async () => {
		const p = await page([events([text('ok'), completed(1)])], RESPONSES);
		const reply: Message = {
			role: 'assistant',
			rounds: [
				{
					reasoning: 'r',
					text: '',
					calls: [],
					opaque: { protocol: 'responses', items: [{ type: 'reasoning', id: 'rs' }] }
				}
			]
		};
		const { turn } = await import('../src/lib/agent.js');
		const history: Message[] = [{ role: 'user', text: 'a' }, reply, { role: 'user', text: 'b' }];
		await turn(history, { role: 'assistant', rounds: [] }, p.tools, p.stop.signal, pulse(0));
		expect((p.bodies[0] as unknown as { input: object[] }).input).toEqual([
			{ role: 'user', content: 'a' },
			{ role: 'user', content: 'b' }
		]);
	});

	it('fails with the message of a response that failed', async () => {
		const p = await page(
			[
				events([
					text('par'),
					{ type: 'response.failed', response: { error: { message: 'the model failed' } } }
				])
			],
			RESPONSES
		);
		await expect(p.go()).rejects.toThrow('the model failed');
	});
});
