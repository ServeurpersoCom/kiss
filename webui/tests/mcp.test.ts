import { beforeEach, describe, expect, it, vi } from 'vitest';
import {
	Server,
	WebStandardStreamableHTTPServerTransport,
	createMcpHandler
} from '@modelcontextprotocol/server';
import type { Assistant, Entry, Message, Round, ToolContext } from '../src/lib/types.js';
import type { Grant, Verdict } from '../src/engine/engine.js';
import { ALWAYS, ONCE, REFUSE } from '../src/engine/engine.js';
import { line, pulse } from '../src/lib/pulse.js';
import { STOPPED, settled } from '../src/lib/conversation.js';

interface Body {
	messages: { role: string; content: string }[];
	tools: { function: { name: string } }[];
}

// what a call asked again carries: the answers of the user
interface Again {
	inputResponses?: Record<string, unknown>;
}

// a tool as mcp.js declares one: a raw JSON schema with a description argument
interface Declared {
	name: string;
	run: (args: Record<string, unknown>, signal: AbortSignal, again: Again) => Promise<object>;
}

const PNG = 'iVBORw0KGgo=';

// what a server says of itself
interface Info {
	name: string;
	version: string;
	title?: string;
	description?: string;
}

const FIXTURE: Info = { name: 'fixture', version: '1.0.0' };

// a KiSS server: its system as its title, the line it writes of itself
const FORGE: Info = {
	name: 'forge',
	version: 'c6a4f05 (2026-10-11)',
	title: 'KiSS Operating System',
	description: 'server forge c6a4f05 (2026-10-11) on linux'
};

// a server with its tools and, when it is a KiSS server, the routes it
// publishes, each by name with whether it holds a key
function server(tools: Declared[], info = FIXTURE, routes?: Record<string, boolean>): Server {
	const capabilities = routes ? { tools: {}, resources: {} } : { tools: {} };
	const s = new Server(info, { capabilities });
	if (routes)
		s.setRequestHandler('resources/list', () => ({
			resources: Object.entries(routes).map(([name, keyed]) => ({
				uri: `kiss:proxy/${name}`,
				name,
				_meta: { 'kiss/keyed': keyed }
			}))
		}));
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
		const again = { inputResponses: ctx.mcpReq.inputResponses };
		return tool.run(req.params.arguments ?? {}, ctx.mcpReq.signal, again) as never;
	});
	return s;
}

// the shape of mcp.js: one stateless server and transport per POST, 2025 era
function legacy(tools: Declared[], info = FIXTURE) {
	return async (req: Request) => {
		const transport = new WebStandardStreamableHTTPServerTransport({
			sessionIdGenerator: undefined
		});
		await server(tools, info).connect(transport);
		return transport.handleRequest(req);
	};
}

// a 2026 era endpoint
function modern(tools: Declared[], info = FIXTURE, routes?: Record<string, boolean>) {
	const handler = createMcpHandler(() => server(tools, info, routes));
	return (req: Request) => handler.fetch(req);
}

const text = (t: string) => async () => ({ content: [{ type: 'text', text: t }] });

// a call that asks the user for a secret before it is asked again with it
const asking = () => ({
	resultType: 'input_required',
	inputRequests: {
		secrets: {
			method: 'elicitation/create',
			params: {
				mode: 'form',
				message: 'Type it',
				requestedSchema: {
					type: 'object',
					properties: { token: { type: 'string' } },
					required: ['token']
				}
			}
		}
	}
});
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
	const engine = await import('../src/config.js');
	engine.start();
	await engine.run('set endpoints url m http://m/v1\nset chat model m/x', 'user');
	const { turn } = await import('../src/lib/agent.js');
	const go = async () => {
		const stop = new AbortController();
		const grant = async (request: Grant) => (asked.push(request), verdicts.shift() ?? ONCE);
		const reply: Assistant = { role: 'assistant', rounds: [] };
		// the rounds as the page last stored them
		const kept: { rounds: Round[] } = { rounds: [] };
		const tools: ToolContext = {
			signal: stop.signal,
			cli: (l) => engine.run(l, 'llm', { signal: stop.signal, grant }),
			redact: engine.redact,
			grant,
			secret: async (key, note) => (keys.push(key), notes.push(note), secrets.shift() ?? null),
			confirm: async (question) => (questions.push(question), yeses.shift() ?? false),
			keep: async () => void (kept.rounds = settled(reply.rounds))
		};
		const user: Message[] = [{ role: 'user', text: 'go' }];
		const clock = pulse(performance.now());
		return { reply, stop, clock, kept, done: turn(user, reply, tools, stop.signal, clock) };
	};
	// what the model asked the user, answered by the verdicts in order, once past them
	const asked: Grant[] = [];
	const verdicts: Verdict[] = [];
	// the secrets the user types, in order, none once past them, and the keys
	// they were asked for
	const secrets: string[] = [];
	const keys: string[] = [];
	// the notes of the servers that asked
	const notes: string[] = [];
	// what the user confirms, in order, no once past them, and what they were asked
	const yeses: boolean[] = [];
	const questions: string[] = [];
	return {
		engine,
		bodies,
		auth,
		methods,
		asked,
		verdicts,
		secrets,
		keys,
		notes,
		yeses,
		questions,
		go
	};
}

// the address the page is served at, against which a relative URL resolves
function serve(url: string): void {
	(window as unknown as { happyDOM: { setURL(url: string): void } }).happyDOM.setURL(url);
}

beforeEach(() => {
	localStorage.clear();
	vi.unstubAllGlobals();
	serve('http://kiss/');
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

	it('lists its tools again as each turn starts, the same from round to round', async () => {
		const tools: Declared[] = [];
		tools.push({
			name: 'grow',
			run: async () => (
				tools.push({ name: 'grown', run: text('new') }),
				{ content: [{ type: 'text', text: 'ok' }] }
			)
		});
		const p = await page({ a: modern(tools) }, [
			stream([call('grow', {})]),
			stream([{ content: 'one' }]),
			stream([{ content: 'two' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp\nset tools use grow on', 'user');
		await (
			await p.go()
		).done;
		await (
			await p.go()
		).done;
		const names = (i: number) => p.bodies[i].tools.map((t) => t.function.name);
		expect(names(0)).toEqual(['config', 'grow']);
		expect(names(1)).toEqual(names(0));
		expect(names(2)).toEqual(['config', 'grow', 'grown']);
	});

	it('of KiSS shows in show version with the line it gives of itself, in either era', async () => {
		const p = await page(
			{ a: legacy(SHELL, FORGE), b: modern(SHELL, FORGE), c: modern(SHELL) },
			[]
		);
		await p.engine.run(
			'set mcp url a http://a/mcp\nset mcp url b http://b/mcp\nset mcp url c http://c/mcp',
			'user'
		);
		const lines = (await p.engine.run('show version', 'user')).text.split('\n');
		expect(lines[0]).toBe('! KiSS Operating System');
		expect(lines[1]).toMatch(/^! page /);
		expect(lines.slice(2)).toEqual([
			'! server forge c6a4f05 (2026-10-11) on linux',
			'! server forge c6a4f05 (2026-10-11) on linux'
		]);
	});

	it('carries the key as a bearer token', async () => {
		const p = await page({ a: legacy(SHELL) }, []);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp key a sk-mcp', 'user');
		expect((await p.engine.run('show tools', 'user')).text).toContain(
			'! mcp a\nset tools use bash_tool consent'
		);
		expect(p.auth.length).toBeGreaterThan(0);
		expect(p.auth.every((a) => a === 'Bearer sk-mcp')).toBe(true);
	});

	it('at a relative URL resolves against the page, which keeps the URL as typed', async () => {
		serve('http://kiss/app/');
		const paths: string[] = [];
		const p = await page(
			{ kiss: (req) => (paths.push(new URL(req.url).pathname), modern(SHELL)(req)) },
			[]
		);
		await p.engine.run('set mcp url a mcp\nset mcp url b /mcp', 'user');
		expect((await p.engine.run('show running-config', 'user')).text).toContain(
			'set mcp url a mcp\nset mcp url b /mcp'
		);
		await p.engine.run('show tools', 'user');
		expect([...new Set(paths)].sort()).toEqual(['/app/mcp', '/mcp']);
	});

	it('asks the user the key it takes no request without, again while the key is wrong', async () => {
		const open = modern(SHELL);
		const p = await page(
			{
				lock: (req) =>
					req.headers.get('authorization') === 'Bearer good'
						? open(req)
						: Promise.resolve(new Response('the key is missing or wrong', { status: 401 }))
			},
			[]
		);
		await p.engine.run('set mcp url local http://lock/mcp', 'user');
		vi.doMock('../src/lib/db.js', () => ({
			listConversations: async () => [],
			putConversations: async () => {},
			deleteConversations: async () => {}
		}));
		const state = await import('../src/lib/state.svelte.js');
		const unlocked = state.unlock();
		for (const value of ['bad', 'good']) {
			await vi.waitFor(() => expect(state.app.asks[0]?.kind).toBe('secret'));
			const ask = state.app.asks[0];
			if (ask.kind === 'secret') ask.settle(value);
		}
		await unlocked;
		const cli = state.app.current!.entries.map((e) => (e.role === 'cli' ? e.output : ''));
		expect(cli).toEqual([
			'! the user typed mcp key local\n+ ! mcp key local is set',
			'! the user typed mcp key local\n! mcp key local changed'
		]);
		expect((await p.engine.run('show tools', 'user')).text).toContain('bash_tool');
		await p.engine.run('no mcp key local', 'user');
		const refused = state.unlock();
		await vi.waitFor(() => expect(state.app.asks[0]?.kind).toBe('secret'));
		const ask = state.app.asks[0];
		if (ask.kind === 'secret') ask.settle(null);
		await refused;
		expect(state.app.asks).toEqual([]);
	});

	it('asks the endpoint again while it answers a status that passes, as many times as set', async () => {
		const busy = () => new Response('{"error":{"message":"busy"}}', { status: 429 });
		const p = await page({}, [
			busy(),
			new Response('', { status: 503 }),
			stream([{ content: 'ok' }])
		]);
		await p.engine.run('set endpoints retry-pause m 0', 'user');
		const t = await p.go();
		await t.done;
		expect(t.reply.rounds[0].text).toBe('ok');
		expect(p.bodies).toHaveLength(3);
		await p.engine.run('set endpoints max-retries m 1', 'user');
		const replies = [busy(), busy()];
		vi.stubGlobal('fetch', async () => replies.shift()!);
		const u = await p.go();
		await expect(u.done).rejects.toThrow('answers 429 busy');
	});

	it('takes no url another server has, a relative one resolved against the page', async () => {
		serve('http://kiss/app/');
		const p = await page({ kiss: modern(SHELL) }, []);
		await p.engine.run('set mcp url local mcp', 'user');
		expect((await p.engine.run('set mcp url pod http://kiss/app/mcp', 'user')).text).toContain(
			'mcp pod has the url of mcp local'
		);
		expect((await p.engine.run('set mcp url pod /mcp', 'user')).ok).toBe(true);
	});

	it('sends and stores no secret a server answers back, escaped or not, the screen showing it', async () => {
		const ECHO: Declared = {
			name: 'echo',
			run: async (_args, _signal, again) => {
				if (!again.inputResponses) return asking();
				const { content } = again.inputResponses.secrets as { content: { token: string } };
				return { content: [{ type: 'text', text: `token ${content.token} taken` }] };
			}
		};
		const refusing = async () =>
			new Response('{"error":{"message":"bad key \\u0073k-proj-endpoint-0001"}}', {
				status: 401,
				headers: { 'content-type': 'application/json' }
			});
		const p = await page({ k: refusing, a: modern([ECHO]) }, [
			stream([call('config', { description: 'list', lines: 'show models' })]),
			stream([call('echo', { description: 'token' })]),
			stream([{ content: 'done' }])
		]);
		await p.engine.run(
			'set endpoints url k http://k/v1\nset endpoints key k sk-proj-endpoint-0001\n' +
				'set mcp url a http://a/mcp\nset tools use echo on',
			'user'
		);
		p.secrets.push('typed-secret-0002');
		const t = await p.go();
		await t.done;
		expect(t.reply.rounds[0].calls[0].result).toContain('bad key sk-proj-endpoint-0001');
		const sent = JSON.stringify(p.bodies);
		const { serialize } = await import('../src/lib/conversation.js');
		const kept = serialize([
			{ id: 'c', title: 't', updated: 0, leaf: null, entries: [{ ...t.reply, id: 'e' } as Entry] }
		]);
		for (const out of [sent, kept]) {
			expect(out).not.toContain('sk-proj-endpoint-0001');
			expect(out).not.toContain('typed-secret-0002');
			expect(out).toContain('bad key <removed>');
			expect(out).toContain('token <removed> taken');
		}
	});

	it('asks the user for the secrets a server wants, then asks the call again with them', async () => {
		// the answers each call carried, none on a first call
		const answers: unknown[] = [];
		const ASK: Declared = {
			name: 'ask',
			run: async (_args, _signal, again) => {
				answers.push(again.inputResponses ?? null);
				if (!again.inputResponses) return asking();
				const { action, content } = again.inputResponses.secrets as {
					action: string;
					content?: { token: string };
				};
				return { content: [{ type: 'text', text: content?.token ?? action }], isError: !content };
			}
		};
		const p = await page({ a: modern([ASK]) }, [
			stream([call('ask', { description: 'key' })]),
			stream([call('ask', { description: 'key' })]),
			stream([{ content: 'done' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		p.secrets.push('s3cret');
		const t = await p.go();
		await t.done;
		expect(p.keys).toEqual(['token of a', 'token of a']);
		expect(p.notes).toEqual(['Type it', 'Type it']);
		expect(answers).toEqual([
			null,
			{ secrets: { action: 'accept', content: { token: 's3cret' } } },
			null,
			{ secrets: { action: 'cancel' } }
		]);
		const calls = t.reply.rounds.flatMap((r) => r.calls);
		expect(calls[0]).toMatchObject({ result: 's3cret', ok: true });
		expect(calls[1]).toMatchObject({ result: 'cancel', ok: false });
	});

	it('asks the user to confirm what a server says, then asks the call again with the answer', async () => {
		const answers: unknown[] = [];
		const CONFIRM: Declared = {
			name: 'route',
			run: async (_args, _signal, again) => {
				answers.push(again.inputResponses ?? null);
				if (again.inputResponses) {
					const { action } = again.inputResponses.confirm as { action: string };
					return { content: [{ type: 'text', text: action }] };
				}
				return {
					resultType: 'input_required',
					inputRequests: {
						confirm: {
							method: 'elicitation/create',
							params: {
								mode: 'form',
								message: 'Route x to https://x?',
								requestedSchema: {
									type: 'object',
									properties: { 'mcp url x': { type: 'boolean' } },
									required: ['mcp url x']
								}
							}
						}
					}
				};
			}
		};
		const p = await page({ a: modern([CONFIRM]) }, [
			stream([call('route', { description: 'route' })]),
			stream([call('route', { description: 'route' })]),
			stream([{ content: 'done' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		p.yeses.push(true, false);
		await (
			await p.go()
		).done;
		expect(p.questions).toEqual(['Route x to https://x?', 'Route x to https://x?']);
		expect(answers).toEqual([
			null,
			{ confirm: { action: 'accept', content: { 'mcp url x': true } } },
			null,
			{ confirm: { action: 'cancel' } }
		]);
	});

	it('reaches a server through the KiSS server that proxies it, with the key of that server', async () => {
		const own = modern(SHELL, FIXTURE, { exa: true });
		const proxied = modern([{ name: 'search', run: text('hit') }]);
		const p = await page(
			{
				k: (req) => (new URL(req.url).pathname === '/proxy/exa' ? proxied(req) : own(req)),
				e: modern([{ name: 'search', run: text('direct') }])
			},
			[]
		);
		await p.engine.run(
			'set mcp url kiss http://k/mcp\nset mcp key kiss K\nset mcp proxy exa kiss',
			'user'
		);
		expect((await p.engine.run('show tools', 'user')).text).toContain(
			'! mcp exa\nset tools use search'
		);
		expect(p.methods.e).toBeUndefined();
		expect(p.auth).toContain('Bearer K');
		expect(p.auth).not.toContain('Bearer E');
		await p.engine.run('no mcp proxy exa\nset mcp url exa http://e/mcp\nset mcp key exa E', 'user');
		await p.engine.run('show tools', 'user');
		expect(p.methods.e).toContain('tools/list');
		expect(p.auth).toContain('Bearer E');
	});

	it('takes as a proxy another server that has none itself', async () => {
		const p = await page({}, []);
		await p.engine.run('set mcp url k http://k/mcp\nset mcp url j http://j/mcp', 'user');
		expect((await p.engine.run('set mcp proxy k k', 'user')).text).toContain(
			'mcp proxy k names the server itself'
		);
		expect((await p.engine.run('set mcp proxy k z', 'user')).text).toContain(
			'mcp proxy k: no mcp z'
		);
		expect((await p.engine.run('set mcp proxy k j\nset mcp proxy j k', 'user')).text).toContain(
			'has a proxy itself'
		);
	});

	it('keeps a secret typed for a server out of the model and the conversation', async () => {
		const SECRET = 's3cret-kept';
		const KEEP: Declared = {
			name: 'keep',
			run: async (_args, _signal, again) =>
				again.inputResponses ? { content: [{ type: 'text', text: 'kept' }] } : asking()
		};
		const p = await page({ a: modern([KEEP]) }, [
			stream([call('keep', { description: 'key' })]),
			stream([{ content: 'done' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		p.secrets.push(SECRET);
		const t = await p.go();
		await t.done;
		expect(t.reply.rounds.flatMap((r) => r.calls)[0]).toMatchObject({ result: 'kept', ok: true });
		expect(JSON.stringify(p.bodies)).not.toContain(SECRET);
		expect(JSON.stringify(t.reply)).not.toContain(SECRET);
		expect(JSON.stringify(t.kept)).not.toContain(SECRET);
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
			result: 'shot\nImage image/png, 8 bytes',
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
		const saved = await p.engine.run('copy running-config a', 'user');
		expect(saved.text).toMatch(/^! copied running-config to a\n! mcp gone/);
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
			stream([call('config', { lines: 'show display' })]),
			stream([call('config', { lines: 'show display' })]),
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

	it('is never reached by a batch of the model before the user lets it, nor closed by one', async () => {
		let models = 0;
		const y = async () => (models++, new Response('{"data":[]}'));
		const p = await page({ w: legacy(SHELL), x: legacy(SHELL), y }, []);
		await p.engine.run('set mcp url w http://w/mcp', 'user');
		await vi.waitFor(() => expect(p.methods.w).toContain('tools/list'));
		const before = p.methods.w.length;
		const refuse = async () => REFUSE;
		const r = await p.engine.run('no mcp w\nset mcp url x http://x/mcp\nshow tools', 'llm', {
			grant: refuse
		});
		expect(r.text).toContain('the user refused the change');
		expect(p.methods.x).toBeUndefined();
		await p.engine.run('set endpoints url y http://y/v1\nshow models', 'llm', { grant: refuse });
		expect(models).toBe(0);
		await p.engine.run('show tools', 'user');
		expect(p.methods.w).toHaveLength(before);
	});

	it('tells what it left out in the one system message, after the system prompt', async () => {
		const p = await page({}, [stream([{ content: 'ok' }])]);
		await p.engine.run(
			'set chat system-prompt "be brief"\nset mcp url gone http://gone/mcp',
			'user'
		);
		await (
			await p.go()
		).done;
		const system = p.bodies[0].messages.filter((m) => m.role === 'system');
		expect(system).toHaveLength(1);
		expect(system[0].content).toMatch(/^be brief\n\n[^\n]+\nmcp gone/);
		expect(p.bodies[0].messages[0].role).toBe('system');
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

	it('lists every tool by who serves it, the settings of a tool no longer served last', async () => {
		const p = await page({ a: legacy(SHELL), b: legacy([{ name: 'old', run: text('old') }]) }, []);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp url b http://b/mcp', 'user');
		await p.engine.run('set tools use snap off\nset tools use old on', 'user');
		await p.engine.run('no mcp b', 'user');
		expect((await p.engine.run('show tools use', 'user')).text).toBe(
			[
				'! KiSS',
				'set tools use config on',
				'! mcp a',
				'set tools use bash_tool consent',
				'set tools use snap off',
				'set tools use fail consent',
				'set tools use wait consent',
				'! unavailable',
				'set tools use old on'
			].join('\n')
		);
	});

	it('turned off by a call is reached by no later call of the same round', async () => {
		const off = {
			name: 'config',
			arguments: JSON.stringify({ lines: 'set tools use bash_tool off' })
		};
		const bash = {
			name: 'bash_tool',
			arguments: JSON.stringify({ description: 'x', text: 'leak' })
		};
		const p = await page({ a: legacy(SHELL) }, [
			stream([
				{
					tool_calls: [
						{ index: 0, id: 'a', function: off },
						{ index: 1, id: 'b', function: bash }
					]
				}
			]),
			stream([{ content: 'ok' }])
		]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		await (
			await p.go()
		).done;
		expect(p.asked).toEqual([]);
		expect(p.methods.a).not.toContain('tools/call');
		expect(p.engine.settings.get('tools use', 'bash_tool')).toBe('off');
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

	it('keeps a call it stops as stopped, its arguments as sent, stored so before it runs', async () => {
		const p = await page({ a: legacy(SHELL) }, [stream([call('wait', { description: 'hold' })])]);
		await p.engine.run('set mcp url a http://a/mcp', 'user');
		const t = await p.go();
		await vi.waitFor(() => expect(t.reply.rounds[0]?.calls.length).toBe(1));
		await new Promise((r) => setTimeout(r, 50));
		const stopped = {
			id: expect.any(String),
			name: 'wait',
			args: '{"description":"hold"}',
			result: STOPPED,
			ok: false
		};
		expect(t.kept.rounds[0].calls).toEqual([stopped]);
		t.stop.abort();
		await expect(t.done).rejects.toThrow();
		expect(t.reply.rounds[0].calls).toEqual([stopped]);
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
			'The user refused the call',
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

	it('holding a key moves with it once the user lets the model move it', async () => {
		const p = await page({ a: legacy(SHELL) }, []);
		await p.engine.run('set mcp url a http://a/mcp\nset mcp key a sk-mcp', 'user');
		const asked: Grant[] = [];
		const grant = async (request: Grant) => (asked.push(request), REFUSE);
		expect((await p.engine.run('set mcp url a http://b/mcp', 'llm', { grant })).text).toBe(
			'% the user refused the change'
		);
		expect(asked).toEqual([
			{
				kind: 'change',
				lines: [
					'- set mcp url a http://a/mcp',
					'+ set mcp url a http://b/mcp',
					'! mcp key a goes with it'
				]
			}
		]);
	});
});
