import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
	Server,
	WebStandardStreamableHTTPServerTransport,
	createMcpHandler
} from '@modelcontextprotocol/server';
import type { Assistant, Grant, Message, ToolContext, Verdict } from '../src/lib/types.js';
import { ALWAYS, ONCE, REFUSE } from '../src/lib/types.js';
import { line, pulse } from '../src/lib/pulse.js';

interface Body {
	messages: { role: string; content: string }[];
	tools: { function: { name: string } }[];
}

// a tool as mcp.js declares one: a raw JSON schema with a description argument
interface Declared {
	name: string;
	run: (args: Record<string, unknown>, signal: AbortSignal) => Promise<object>;
}

const PNG = 'iVBORw0KGgo=';

function server(tools: Declared[]): Server {
	const s = new Server({ name: 'fixture', version: '1.0.0' }, { capabilities: { tools: {} } });
	s.setRequestHandler('tools/list', () => ({
		tools: tools.map((t) => ({
			name: t.name,
			description: `the ${t.name} tool`,
			inputSchema: {
				type: 'object' as const,
				properties: { description: { type: 'string' }, text: { type: 'string' } },
				required: ['description']
			}
		}))
	}));
	s.setRequestHandler('tools/call', (req, ctx) => {
		const tool = tools.find((t) => t.name === req.params.name)!;
		return tool.run(req.params.arguments ?? {}, ctx.mcpReq.signal) as never;
	});
	return s;
}

// the shape of mcp.js: one stateless server and transport per POST, 2025 era
function legacy(tools: Declared[]) {
	return async (req: Request) => {
		const transport = new WebStandardStreamableHTTPServerTransport({
			sessionIdGenerator: undefined
		});
		await server(tools).connect(transport);
		return transport.handleRequest(req);
	};
}

// a 2026 era endpoint
function modern(tools: Declared[]) {
	const handler = createMcpHandler(() => server(tools));
	return (req: Request) => handler.fetch(req);
}

const text = (t: string) => async () => ({ content: [{ type: 'text', text: t }] });

const SHELL: Declared[] = [
	{
		name: 'bash_tool',
		run: async (args) => ({ content: [{ type: 'text', text: `ran ${args.text}` }] })
	},
	{
		name: 'snap',
		run: async () => ({
			content: [
				{ type: 'text', text: 'shot' },
				{ type: 'image', data: PNG, mimeType: 'image/png' }
			]
		})
	},
	{ name: 'fail', run: async () => ({ content: [{ type: 'text', text: 'no' }], isError: true }) },
	{
		name: 'wait',
		run: (_args, signal) =>
			new Promise((_, reject) => signal.addEventListener('abort', () => reject(signal.reason)))
	}
];

// the answers of the model, one stream per request
function stream(deltas: object[]): Response {
	const events = deltas.map((d) => `data: ${JSON.stringify({ choices: [{ delta: d }] })}\n\n`);
	return new Response(events.join('') + 'data: [DONE]\n\n', {
		headers: { 'content-type': 'text/event-stream' }
	});
}

function call(name: string, args: object): object {
	return {
		tool_calls: [{ index: 0, id: `c-${name}`, function: { name, arguments: JSON.stringify(args) } }]
	};
}

// a page whose model answers each request with the next reply, and whose MCP
// servers answer on the hosts the routes name; the model offers its own
// endpoint only
async function page(
	routes: Record<string, (req: Request) => Promise<Response>>,
	replies: Response[]
) {
	vi.resetModules();
	const bodies: Body[] = [];
	const auth: (string | null)[] = [];
	// the JSON-RPC methods each server received
	const methods: Record<string, string[]> = {};
	vi.stubGlobal('fetch', async (input: RequestInfo | URL, init?: RequestInit) => {
		const req = new Request(input, init);
		const url = new URL(req.url);
		if (url.host === 'm') {
			if (url.pathname.endsWith('/models')) {
				return new Response('{"data":[{"id":"x"}]}', {
					headers: { 'content-type': 'application/json' }
				});
			}
			bodies.push(JSON.parse(await req.text()));
			return replies.shift()!;
		}
		const route = routes[url.host];
		if (!route) throw new TypeError(`fetch failed: ${url.host}`);
		auth.push(req.headers.get('authorization'));
		const sent = req.method === 'POST' ? await req.clone().text() : '';
		if (sent) (methods[url.host] ??= []).push(JSON.parse(sent).method);
		return new Promise<Response>((resolve, reject) => {
			init?.signal?.addEventListener('abort', () => reject(init.signal!.reason));
			route(req).then(resolve, reject);
		});
	});
	const engine = await import('../src/engine/run.js');
	engine.start();
	await engine.run('set endpoints url m http://m/v1\nset chat model m/x', 'user');
	const { turn } = await import('../src/lib/agent.js');
	const go = async () => {
		const stop = new AbortController();
		const grant = async (request: Grant) => (asked.push(request), verdicts.shift() ?? ONCE);
		const tools: ToolContext = {
			signal: stop.signal,
			cli: (l) => engine.run(l, 'llm', { signal: stop.signal, grant }),
			redact: engine.redact,
			grant
		};
		const reply: Assistant = { role: 'assistant', rounds: [] };
		const user: Message[] = [{ role: 'user', text: 'go' }];
		const clock = pulse(performance.now());
		return { reply, stop, clock, done: turn(user, reply, tools, stop.signal, clock) };
	};
	// what the model asked the user, answered by the verdicts in order, once past them
	const asked: Grant[] = [];
	const verdicts: Verdict[] = [];
	return { engine, bodies, auth, methods, asked, verdicts, go };
}

beforeEach(() => {
	localStorage.clear();
	vi.unstubAllGlobals();
});

describe('an MCP server', () => {
	it('speaks the era it answers in: 2026, else the initialize handshake', async () => {
		const p = await page({ a: legacy(SHELL), b: modern(SHELL) }, []);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp url b http://b/mcp', 'user');
		await p.engine.run('show tools', 'user');
		expect(p.methods.a).toEqual([
			'server/discover',
			'initialize',
			'notifications/initialized',
			'tools/list'
		]);
		expect(p.methods.b).toEqual(['server/discover', 'tools/list']);
	});

	it('serves its tools under their own names, after the own ones of KiSS', async () => {
		const p = await page({ a: legacy(SHELL), b: modern([{ name: 'search', run: text('hit') }]) }, [
			stream([{ content: 'one' }]),
			stream([{ content: 'two' }])
		]);
		await p.engine.run('set mcp url b http://b/mcp\nset mcp url a http://a/mcp', 'user');
		await (
			await p.go()
		).done;
		await (
			await p.go()
		).done;
		const names = p.bodies[0].tools.map((t) => t.function.name);
		expect(names).toEqual(['config', 'bash_tool', 'snap', 'fail', 'wait', 'search']);
		expect(p.bodies[1].tools).toEqual(p.bodies[0].tools);
	});

	it('carries the key as a bearer token', async () => {
		const p = await page({ a: legacy(SHELL) }, []);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp key a sk-mcp', 'user');
		expect((await p.engine.run('show tools', 'user')).text).toContain(
			'! mcp a\n set tools use bash_tool consent'
		);
		expect(p.auth.length).toBeGreaterThan(0);
		expect(p.auth.every((a) => a === 'Bearer sk-mcp')).toBe(true);
	});

	it('answers a call with its text, its images and its error flag', async () => {
		const p = await page({ a: legacy(SHELL) }, [
			stream([call('bash_tool', { description: 'list', text: 'ls' })]),
			stream([call('snap', { description: 'look' })]),
			stream([call('fail', { description: 'try' })]),
			stream([{ content: 'done' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		const t = await p.go();
		await t.done;
		const calls = t.reply.rounds.flatMap((r) => r.calls);
		expect(calls[0]).toMatchObject({ result: 'ran ls', ok: true });
		expect(calls[1]).toMatchObject({
			result: 'shot\n! image image/png, 8 bytes',
			ok: true,
			images: [{ mime: 'image/png', data: PNG }]
		});
		expect(calls[2]).toMatchObject({ result: 'no', ok: false });
	});

	it('that does not answer serves nothing, the model told and the save warning of it', async () => {
		const p = await page({ a: legacy(SHELL) }, [stream([{ content: 'ok' }])]);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp url gone http://gone/mcp', 'user');
		await (
			await p.go()
		).done;
		expect(p.bodies[0].tools.map((t) => t.function.name)).toEqual([
			'config',
			'bash_tool',
			'snap',
			'fail',
			'wait'
		]);
		expect(p.bodies[0].messages[0]).toMatchObject({ role: 'system' });
		expect(p.bodies[0].messages[0].content).toContain('mcp gone');
		const saved = await p.engine.run('save a', 'user');
		expect(saved.text).toMatch(/^! saved a\n! mcp gone/);
		expect((await p.engine.run('show tools', 'user')).text).toContain('! mcp gone');
	});

	it('connects as soon as the configuration names it, before any turn', async () => {
		const p = await page({ a: legacy(SHELL) }, []);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		await vi.waitFor(() => expect(p.methods.a).toContain('tools/list'));
	});

	it('that fails sits out the rest of the turn, and is tried again the next', async () => {
		let tries = 0;
		const down = async (): Promise<Response> => {
			tries++;
			throw new TypeError('fetch failed: down');
		};
		const p = await page({ down }, [
			stream([call('config', { lines: 'show version' })]),
			stream([call('config', { lines: 'show version' })]),
			stream([{ content: 'ok' }]),
			stream([{ content: 'again' }])
		]);
		await p.engine.run('set mcp url down http://down/mcp', 'user');
		await vi.waitFor(() => expect(tries).toBeGreaterThan(0));
		const before = tries;
		await (
			await p.go()
		).done;
		const once = tries - before;
		expect(once).toBeGreaterThan(0);
		expect(p.bodies).toHaveLength(3);
		for (const b of p.bodies) expect(b.messages[0].content).toContain('mcp down');
		await (
			await p.go()
		).done;
		expect(tries - before).toBe(2 * once);
	});

	it('that failed is tried again within the turn once its key changes', async () => {
		const open = legacy(SHELL);
		const locked = async (req: Request) =>
			req.headers.get('authorization') === 'Bearer good'
				? open(req)
				: new Response('{"error":"unauthorized"}', { status: 401 });
		const p = await page({ locked }, [
			stream([call('config', { lines: 'set mcp key locked good' })]),
			stream([{ content: 'ok' }])
		]);
		await p.engine.run('set mcp url locked http://locked/mcp', 'user');
		await (
			await p.go()
		).done;
		expect(p.bodies[0].messages[0].content).toContain('mcp locked');
		expect(p.bodies[1].tools.map((t) => t.function.name)).toContain('bash_tool');
		// the model still reads why it asked for the key, the server back
		expect(p.bodies[1].messages[0].content).toBe(p.bodies[0].messages[0].content);
	});

	it('names the servers a round waits for in its line', async () => {
		let list!: () => void;
		const listed = new Promise<void>((resolve) => (list = resolve));
		const slow = legacy(SHELL);
		const p = await page({ slow: async (req) => (await listed, slow(req)) }, [
			stream([{ content: 'ok' }])
		]);
		await p.engine.run('set mcp url slow http://slow/mcp', 'user');
		const turn = await p.go();
		await vi.waitFor(() =>
			expect(line(turn.clock, performance.now())).toMatch(/^Preparing mcp slow - /)
		);
		list();
		await turn.done;
		expect(p.bodies[0].tools.map((t) => t.function.name)).toContain('bash_tool');
	});

	it('leaves out a name another one serves first, the model told', async () => {
		const p = await page(
			{
				a: legacy(SHELL),
				b: modern([
					{ name: 'bash_tool', run: text('x') },
					{ name: 'config', run: text('x') }
				])
			},
			[stream([{ content: 'ok' }])]
		);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp url b http://b/mcp', 'user');
		await (
			await p.go()
		).done;
		expect(p.bodies[0].tools.map((t) => t.function.name)).toEqual([
			'config',
			'bash_tool',
			'snap',
			'fail',
			'wait'
		]);
		const told = p.bodies[0].messages[0].content;
		expect(told).toContain('mcp b: tool bash_tool left out, mcp a serves it');
		expect(told).toContain('mcp b: tool config left out, KiSS serves it');
	});

	it('lists every tool by who serves it, the settings set alone last', async () => {
		const p = await page({ a: legacy(SHELL) }, []);
		await p.engine.run(
			'set mcp url a http://a/mcp\nset tools use snap off\nset tools preview old x',
			'user'
		);
		expect((await p.engine.run('show tools use', 'user')).text).toBe(
			[
				'! KiSS',
				' set tools use config on',
				'! mcp a',
				' set tools use bash_tool consent',
				' set tools use snap off',
				' set tools use fail consent',
				' set tools use wait consent',
				'! others',
				' set tools use old consent'
			].join('\n')
		);
	});

	it('turned off is never seen by the model, and frees its name', async () => {
		const p = await page({ a: legacy(SHELL), b: modern([{ name: 'bash_tool', run: text('x') }]) }, [
			stream([{ content: 'ok' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp url b http://b/mcp', 'user');
		await p.engine.run('set tools use bash_tool off\nset tools use config off', 'user');
		await (
			await p.go()
		).done;
		expect(p.bodies[0].tools.map((t) => t.function.name)).toEqual(['snap', 'fail', 'wait']);
	});

	it('fails a call past the timeout of its server', async () => {
		const p = await page({ a: legacy(SHELL) }, [
			stream([call('wait', { description: 'hold' })]),
			stream([{ content: 'ok' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp timeout a 0.1', 'user');
		const t = await p.go();
		await t.done;
		expect(t.reply.rounds[0].calls[0]).toMatchObject({ ok: false });
		expect(t.reply.rounds[0].calls[0].result).toContain('mcp a');
	});

	it('runs no call past a stop', async () => {
		const p = await page({ a: legacy(SHELL) }, [stream([call('wait', { description: 'hold' })])]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		const t = await p.go();
		await vi.waitFor(() => expect(t.reply.rounds[0]?.calls.length).toBe(1));
		await new Promise((r) => setTimeout(r, 50));
		t.stop.abort();
		await expect(t.done).rejects.toThrow();
		expect(t.reply.rounds).toEqual([]);
	});

	it('in consent asks before each call: once lets it, always turns it on, refuse fails it', async () => {
		const p = await page({ a: legacy(SHELL) }, [
			stream([call('bash_tool', { description: 'a', text: 'one' })]),
			stream([call('bash_tool', { description: 'b', text: 'two' })]),
			stream([call('bash_tool', { description: 'c', text: 'three' })]),
			stream([call('bash_tool', { description: 'd', text: 'four' })]),
			stream([{ content: 'done' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		p.verdicts.push(REFUSE, ONCE, ALWAYS);
		const t = await p.go();
		await t.done;
		expect(t.reply.rounds.flatMap((r) => r.calls).map((c) => c.result)).toEqual([
			'the user refused the call',
			'ran two',
			'ran three',
			'ran four'
		]);
		expect(p.asked).toHaveLength(3);
		expect(p.asked[0]).toEqual({
			kind: 'call',
			tool: 'bash_tool',
			args: JSON.stringify({ description: 'a', text: 'one' })
		});
		expect(p.engine.settings.get('tools use', 'bash_tool')).toBe('on');
	});

	it('holding a key is never moved by the model', async () => {
		const p = await page({ a: legacy(SHELL) }, []);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp key a sk-mcp', 'user');
		expect((await p.engine.run('set mcp url a http://evil/mcp', 'llm')).text).toContain(
			'mcp a holds a secret'
		);
	});
});
