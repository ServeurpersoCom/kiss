import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ConfigReader } from '../src/engine/engine.js';
import { learn } from '../src/engine/keychain.js';
import { ATTACHED_STORAGE_KEY } from '../src/lib/config.js';
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
		? { 'mcp url backend': 'http://kiss/mcp', 'mcp key backend': 'k' }
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

// a server whose stream, a WebSocket, gives the events the test pushes, and
// what the page sent it: each stream it opened, with the headers of its first
// message, and each action
function server() {
	const sent: { url: string; init?: RequestInit; first?: Record<string, string> }[] = [];
	let say: (data: string) => void = () => {};
	let refuse: (reason: string) => void = () => {};
	class Socket {
		onopen?: () => void;
		onmessage?: (m: { data: string }) => void;
		onclose?: (e: { code: number; reason: string }) => void;
		private opened: (typeof sent)[number];
		constructor(url: URL) {
			this.opened = { url: String(url) };
			sent.push(this.opened);
			say = (data) => this.onmessage?.({ data });
			refuse = (reason) => this.onclose?.({ code: 1008, reason });
			queueMicrotask(() => this.onopen?.());
		}
		send(data: string) {
			this.opened.first ??= JSON.parse(data);
		}
		close() {
			queueMicrotask(() => this.onclose?.({ code: 1000, reason: '' }));
		}
	}
	vi.stubGlobal('WebSocket', Socket);
	vi.stubGlobal('fetch', async (url: URL, init?: RequestInit) => {
		sent.push({ url: String(url), init });
		return new Response(JSON.stringify({ id: 'n' }), {
			headers: { 'content-type': 'application/json' }
		});
	});
	return {
		sent,
		push: (e: object) => say(JSON.stringify(e)),
		say: (data: string) => say(data),
		refuse: (reason: string) => refuse(reason)
	};
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
		await vi.waitFor(() => expect(s.sent[0]?.url).toBe('ws://kiss/conversations/events'));
		await vi.waitFor(() => expect(s.sent[0].first?.Authorization).toBe('Bearer k'));
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
		const request = { kind: 'call', tool: 'web_search_exa', args: '{"q":"x"}', question: 1 };
		s.push({ set: ['a', 'asking'], value: request });
		await vi.waitFor(() => expect(app.asks.map((q) => q.from)).toEqual(['a']));
		const ask = app.asks[0];
		expect(ask.kind === 'grant' && ask.request).toEqual(request);
		if (ask.kind === 'grant') ask.settle('always');
		await vi.waitFor(() =>
			expect(s.sent.some((r) => r.url.endsWith('/conversations/answer'))).toBe(true)
		);
		const post = s.sent.find((r) => r.url.endsWith('/conversations/answer'))!;
		expect(JSON.parse(String(post.init?.body))).toEqual({
			id: 'a',
			question: 1,
			verdict: 'always'
		});
		s.push({ set: ['a', 'asking'], value: null });
		s.push({ set: ['a', 'asking'], value: { ...request, question: 2 } });
		await vi.waitFor(() => expect(app.asks.length).toBe(1));
		s.push({ set: ['a', 'asking'], value: null });
		await vi.waitFor(() => expect(app.asks).toEqual([]));
	});

	it('ask the next question of a turn the server asks once the one before it settled, unseen by the page', async () => {
		const s = server();
		app.conversations = [];
		app.asks = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		const call = (question: number) => ({ kind: 'call', tool: 'bash', args: '{}', question });
		s.push({ set: ['a', 'asking'], value: call(1) });
		await vi.waitFor(() => expect(app.asks.length).toBe(1));
		s.push({ set: ['a', 'asking'], value: call(2) });
		await vi.waitFor(() =>
			expect(app.asks.map((q) => q.kind === 'grant' && q.request)).toEqual([call(2)])
		);
		const ask = app.asks[0];
		if (ask.kind === 'grant') ask.settle('once');
		await vi.waitFor(() =>
			expect(s.sent.some((r) => r.url.endsWith('/conversations/answer'))).toBe(true)
		);
		const post = s.sent.find((r) => r.url.endsWith('/conversations/answer'))!;
		expect(JSON.parse(String(post.init?.body))).toEqual({ id: 'a', question: 2, verdict: 'once' });
	});

	it('fill a form a turn of the server asks field by field, the response going to the server, a form taken back asking no more', async () => {
		const s = server();
		app.conversations = [];
		app.asks = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		const form = (fields: Record<string, string>, question: number) => ({
			kind: 'form',
			question,
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
			value: form({ 'endpoints key hf': 'string', 'mcp url x': 'boolean' }, 1)
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
			question: 1,
			response: { action: 'accept', content: { 'endpoints key hf': 'hf_x', 'mcp url x': true } }
		});

		s.push({ set: ['a', 'asking'], value: null });
		s.push({ set: ['a', 'asking'], value: form({ one: 'string', two: 'string' }, 2) });
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

	it('run the lines the server asks of the pages, what they answer going to the server with the secrets of the page sealed', async () => {
		const s = server();
		learn('sk-page-only');
		const ran: string[] = [];
		serve({
			load: async () => undefined,
			run: async (id, lines) => (
				ran.push(`${id} ${lines}`),
				{ ok: true, text: '+ ran with sk-page-only' }
			)
		});
		app.conversations = [];
		follow(config(true));
		s.push({ set: [], value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] } });
		s.push({ set: ['a', 'asking'], value: { kind: 'run', lines: 'show version', question: 2 } });
		await vi.waitFor(() =>
			expect(s.sent.some((r) => r.url.endsWith('/conversations/answer'))).toBe(true)
		);
		expect(ran).toEqual(['a show version']);
		const post = s.sent.find((r) => r.url.endsWith('/conversations/answer'))!;
		expect(JSON.parse(String(post.init?.body))).toEqual({
			id: 'a',
			question: 2,
			output: '+ ran with <removed>',
			ok: true
		});
		expect(app.asks).toEqual([]);
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

	it('attach no page that answers a turn of its own', () => {
		server();
		attach(config(true), false);
		app.replies = { m: {} as never };
		expect(() => attach(config(true), true)).toThrow('a turn answers on the page -> stop it first');
		expect(attached()).toBe(false);
		localStorage.removeItem(ATTACHED_STORAGE_KEY);
		follow(config(true));
		expect(attached()).toBe(false);
		app.replies = {};
		follow(config(true));
		expect(attached()).toBe(true);
	});

	it('read every event a message holds, one per line, an empty one telling the server is there', async () => {
		vi.useFakeTimers({ shouldAdvanceTime: true });
		const s = server();
		app.conversations = [];
		follow(config(true));
		const first = {
			set: [],
			value: { protocol: 1, now: 0, conversations: [conversation('a', 'A', 1)] }
		};
		s.say(
			JSON.stringify(first) + '\n' + JSON.stringify({ set: ['a', 'pinned'], value: true }) + '\n'
		);
		await vi.waitFor(() => expect(app.conversations[0]?.pinned).toBe(true));
		for (let i = 0; i < 4; i++) {
			await vi.advanceTimersByTimeAsync(SYNC_SILENCE_MS - 1000);
			s.say('');
		}
		expect(s.sent.length).toBe(1);
		vi.useRealTimers();
	});

	it('tell a server that refuses the stream, and ask it again', async () => {
		vi.useFakeTimers({ shouldAdvanceTime: true });
		const s = server();
		const errors = vi.spyOn(console, 'error').mockImplementation(() => {});
		app.conversations = [];
		follow(config(true));
		await vi.waitFor(() => expect(s.sent.length).toBe(1));
		s.refuse('the key is missing or wrong');
		await vi.waitFor(() =>
			expect(errors).toHaveBeenCalledWith(expect.stringContaining('the key is missing or wrong'))
		);
		await vi.advanceTimersByTimeAsync(SYNC_RETRY_MS + 100);
		expect(s.sent.length).toBe(2);
		errors.mockRestore();
		vi.useRealTimers();
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
