import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ConfigReader } from '../src/engine/engine.js';
import { app } from '../src/lib/app.svelte.js';
import {
	SYNC_RETRY_MS,
	SYNC_SILENCE_MS,
	act,
	attach,
	attached,
	serve,
	follow,
	turnOf
} from '../src/lib/sync.js';

// the browser database stays out: the conversations live in the page alone
vi.mock('../src/lib/db.js', () => ({
	listConversations: async () => [],
	putConversations: async () => {},
	deleteConversations: async () => {}
}));

// a configuration with the KiSS server backend and its key, which a page
// neither attached nor detached attaches to, or one without it
function config(backend: boolean): ConfigReader {
	const values: Record<string, string> = backend
		? { 'mcp url backend': 'http://kiss/mcp', 'mcp key backend': 'k', 'client hostname': 'abcd' }
		: {};
	const get = (key: string, name?: string) => values[name ? `${key} ${name}` : key];
	return { get, reveal: get, names: (m) => (m === 'mcp' && backend ? ['backend'] : []) };
}

const conversation = (id: string, title: string, updated: number) => ({
	id,
	title,
	updated,
	leaf: `${id}-a`,
	entries: [
		{ role: 'user' as const, text: 'hi', id: `${id}-u`, parent: null, time: updated },
		{ role: 'assistant' as const, rounds: [], id: `${id}-a`, parent: `${id}-u`, time: updated }
	]
});

// a server whose stream gives the events the test pushes, and the requests
// the page sent it
function server() {
	const sent: { url: string; init?: RequestInit }[] = [];
	let push: (event: object) => void = () => {};
	vi.stubGlobal('fetch', async (url: URL, init?: RequestInit) => {
		sent.push({ url: String(url), init });
		if (new URL(url).pathname.endsWith('/conversations/events')) {
			const body = new ReadableStream<Uint8Array>({
				start(c) {
					push = (e) => c.enqueue(new TextEncoder().encode(`data: ${JSON.stringify(e)}\n\n`));
					// a request aborted errors its body, as fetch does
					init?.signal?.addEventListener('abort', () => c.error(init.signal!.reason));
				}
			});
			return new Response(body, { headers: { 'content-type': 'text/event-stream' } });
		}
		return new Response(JSON.stringify({ id: 'n' }), {
			headers: { 'content-type': 'application/json' }
		});
	});
	return { sent, push: (e: object) => push(e) };
}

// the moment of the server as the first event of a stream gives it
const NOW = 1_760_000_000_000;

afterEach(() => {
	localStorage.clear();
	follow(config(false));
	vi.unstubAllGlobals();
});

describe('the conversations of a KiSS server', () => {
	it('follow the stream: every one, each change whole, what a turn grows, the turn, one gone', async () => {
		const s = server();
		const turn = {
			round: 2,
			phase: 'writing',
			name: '',
			status: 0,
			attempt: 0,
			attempts: 0,
			since: NOW - 1500,
			first: NOW - 1000,
			last: NOW - 200,
			tokens: 9
		};
		app.conversations = [];
		follow(config(true));
		await vi.waitFor(() =>
			expect(s.sent[0]?.url).toBe('http://kiss/conversations/events?client=abcd')
		);
		expect(new Headers(s.sent[0].init?.headers).get('authorization')).toBe('Bearer k');
		s.push({
			set: [],
			value: {
				protocol: 1,
				now: NOW,
				conversations: [conversation('a', 'A', 1), conversation('b', 'B', 2)]
			}
		});
		await vi.waitFor(() => expect(app.conversations.map((c) => c.id)).toEqual(['b', 'a']));
		expect(attached()).toBe(true);

		s.push({ set: ['a', 'turn'], value: turn });
		s.push({ add: ['a', 'entries', 1, 'rounds'], value: { reasoning: '', text: '', calls: [] } });
		s.push({ add: ['a', 'entries', 1, 'rounds', 0, 'text'], text: 'Hel' });
		s.push({ add: ['a', 'entries', 1, 'rounds', 0, 'text'], text: 'lo' });
		await vi.waitFor(() => expect(app.replies.a?.rounds[0]?.text).toBe('Hello'));
		// the moments of the server read on the clock of the page, its moment
		// NOW the time the first event came
		const p = turnOf('a')!;
		expect([p.round, p.phase, p.tokens, p.last - p.first]).toEqual([2, 'writing', 9, 800]);
		expect(performance.now() - p.since).toBeGreaterThanOrEqual(1500);
		expect(performance.now() - p.since).toBeLessThan(2500);
		expect(app.replies.a).toBe(app.conversations.find((c) => c.id === 'a')!.entries[1]);

		s.push({ set: ['a', 'turn'], value: null });
		await vi.waitFor(() => expect('a' in app.replies).toBe(false));
		expect(turnOf('a')).toBeUndefined();
		s.push({ set: ['a'], value: { ...conversation('a', 'A2', 3), pinned: true } });
		await vi.waitFor(() =>
			expect(app.conversations[0]).toMatchObject({ id: 'a', title: 'A2', pinned: true })
		);
		s.push({ set: ['b'], value: null });
		await vi.waitFor(() => expect(app.conversations.map((c) => c.id)).toEqual(['a']));
	});

	it('ask what a turn of the server asks, the answer going to the server, the question leaving with it', async () => {
		const s = server();
		app.conversations = [];
		app.asks = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		const request = { kind: 'call', tool: 'web_search_exa', args: '{"q":"x"}' };
		s.push({ set: ['a', 'asking'], value: request });
		await vi.waitFor(() => expect(app.asks.map((q) => q.from)).toEqual(['a']));
		const ask = app.asks[0];
		expect(ask.kind === 'grant' && ask.request).toEqual(request);
		if (ask.kind === 'grant') ask.settle('always');
		await vi.waitFor(() =>
			expect(s.sent.some((r) => r.url.endsWith('/conversations/answer'))).toBe(true)
		);
		const post = s.sent.find((r) => r.url.endsWith('/conversations/answer'))!;
		expect(JSON.parse(String(post.init?.body))).toEqual({ id: 'a', verdict: 'always' });
		s.push({ set: ['a', 'asking'], value: null });
		s.push({ set: ['a', 'asking'], value: request });
		await vi.waitFor(() => expect(app.asks.length).toBe(1));
		s.push({ set: ['a', 'asking'], value: null });
		await vi.waitFor(() => expect(app.asks).toEqual([]));
	});

	it('fill a form a turn of the server asks field by field, the response going to the server, a form taken back asking no more', async () => {
		const s = server();
		app.conversations = [];
		app.asks = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		const form = (fields: Record<string, string>) => ({
			kind: 'form',
			server: 'forge',
			input: {
				method: 'elicitation/create',
				params: {
					mode: 'form',
					message: 'Type it',
					requestedSchema: {
						type: 'object',
						properties: Object.fromEntries(Object.entries(fields).map(([k, type]) => [k, { type }]))
					}
				}
			}
		});
		s.push({
			set: ['a', 'asking'],
			value: form({ 'endpoints key hf': 'string', 'mcp url x': 'boolean' })
		});
		await vi.waitFor(() => expect(app.asks.map((q) => q.kind)).toEqual(['secret']));
		const typed = app.asks[0];
		expect(typed.kind === 'secret' && [typed.key, typed.note]).toEqual([
			'endpoints key hf of forge',
			'Type it'
		]);
		if (typed.kind === 'secret') typed.settle('hf_x');
		await vi.waitFor(() => expect(app.asks.map((q) => q.kind)).toEqual(['confirm']));
		const yes = app.asks[0];
		if (yes.kind === 'confirm') yes.settle(true);
		await vi.waitFor(() =>
			expect(s.sent.some((r) => r.url.endsWith('/conversations/answer'))).toBe(true)
		);
		const post = s.sent.find((r) => r.url.endsWith('/conversations/answer'))!;
		expect(JSON.parse(String(post.init?.body))).toEqual({
			id: 'a',
			response: { action: 'accept', content: { 'endpoints key hf': 'hf_x', 'mcp url x': true } }
		});

		s.push({ set: ['a', 'asking'], value: null });
		s.push({ set: ['a', 'asking'], value: form({ one: 'string', two: 'string' }) });
		await vi.waitFor(() => expect(app.asks.length).toBe(1));
		const first = app.asks[0];
		s.push({ set: ['a', 'asking'], value: null });
		await vi.waitFor(() => expect(app.asks).toEqual([]));
		if (first.kind === 'secret') first.settle('late');
		await new Promise((resolve) => setTimeout(resolve, 10));
		expect(app.asks).toEqual([]);
		expect(s.sent.filter((r) => r.url.endsWith('/conversations/answer')).length).toBe(1);
	});

	it('send the actions of the user to the server, the conversations of the page leaving as it attaches and coming back as it detaches', async () => {
		const s = server();
		const mine = conversation('m', 'M', 1);
		app.conversations = [mine];
		serve({
			load: async () => void (app.conversations = [mine]),
			run: async () => ({ ok: true, text: '' })
		});
		follow(config(true));
		expect(app.conversations).toEqual([]);
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		await vi.waitFor(() => expect(app.conversations.length).toBe(1));
		expect(await act('send', { text: 'hi' })).toEqual({ id: 'n' });
		const post = s.sent.find((r) => r.url === 'http://kiss/conversations/send')!;
		expect(post.init?.method).toBe('POST');
		expect(JSON.parse(String(post.init?.body))).toEqual({ text: 'hi' });
		follow(config(false));
		expect(attached()).toBe(false);
		await vi.waitFor(() => expect(app.conversations).toEqual([mine]));
	});

	it('run the lines the server asks of this page alone, while it holds its name, what they answer going to the server', async () => {
		const s = server();
		const ran: string[] = [];
		serve({
			load: async () => undefined,
			run: async (id, lines) => (ran.push(`${id} ${lines}`), { ok: true, text: '+ ran' })
		});
		app.conversations = [];
		follow(config(true));
		const run = (client: string) => ({ kind: 'run', client, lines: 'show version' });
		s.push({
			set: [],
			value: { protocol: 1, client: 'abcd', now: 0, conversations: [conversation('a', 'A', 1)] }
		});
		s.push({ set: ['a', 'asking'], value: run('efgh') });
		s.push({ set: ['a', 'asking'], value: null });
		s.push({ set: ['a', 'asking'], value: run('abcd') });
		await vi.waitFor(() =>
			expect(s.sent.some((r) => r.url.endsWith('/conversations/answer'))).toBe(true)
		);
		expect(ran).toEqual(['a show version']);
		const post = s.sent.find((r) => r.url.endsWith('/conversations/answer'))!;
		expect(JSON.parse(String(post.init?.body))).toEqual({ id: 'a', output: '+ ran', ok: true });
		expect(app.asks).toEqual([]);
	});

	it('run nothing it is asked under a name another page holds', async () => {
		const s = server();
		const ran: string[] = [];
		serve({
			load: async () => undefined,
			run: async (id) => (ran.push(id), { ok: true, text: '' })
		});
		app.conversations = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		s.push({ set: ['a', 'asking'], value: { kind: 'run', client: 'abcd', lines: 'show version' } });
		await new Promise((resolve) => setTimeout(resolve, 20));
		expect(ran).toEqual([]);
	});

	it('stay detached once detach ran, the backend there or not, until attach runs', async () => {
		const s = server();
		app.conversations = [];
		follow(config(true));
		expect(attached()).toBe(true);
		attach(config(true), false);
		expect(attached()).toBe(false);
		follow(config(true));
		expect(attached()).toBe(false);
		attach(config(true), true);
		expect(attached()).toBe(true);
		await vi.waitFor(() => expect(s.sent.length).toBe(2));
	});

	it('follow no server that speaks another protocol', async () => {
		vi.useFakeTimers({ shouldAdvanceTime: true });
		const s = server();
		const errors = vi.spyOn(console, 'error').mockImplementation(() => {});
		app.conversations = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 2, now: 0, conversations: [conversation('a', 'A', 1)] } });
		await vi.waitFor(() =>
			expect(errors).toHaveBeenCalledWith(expect.stringContaining('protocol 2'))
		);
		await vi.advanceTimersByTimeAsync(SYNC_SILENCE_MS + SYNC_RETRY_MS + 100);
		expect(app.conversations).toEqual([]);
		expect(s.sent.length).toBe(1);
		errors.mockRestore();
		vi.useRealTimers();
	});

	it('open the stream again once it stays silent', async () => {
		vi.useFakeTimers({ shouldAdvanceTime: true });
		const s = server();
		app.conversations = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		await vi.waitFor(() => expect(app.conversations.length).toBe(1));
		await vi.advanceTimersByTimeAsync(SYNC_SILENCE_MS - 500);
		s.push({ set: ['a', 'pinned'], value: true });
		await vi.advanceTimersByTimeAsync(SYNC_SILENCE_MS - 500);
		expect(s.sent.length).toBe(1);
		await vi.advanceTimersByTimeAsync(500 + SYNC_RETRY_MS + 100);
		expect(s.sent.length).toBe(2);
		vi.useRealTimers();
	});
});
