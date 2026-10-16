import { afterEach, describe, expect, it, vi } from 'vitest';
import type { ConfigReader } from '../src/engine/engine.js';
import { app } from '../src/lib/app.svelte.js';
import { act, follow, homed } from '../src/lib/sync.js';

// the browser database stays out: the conversations live in the page alone
vi.mock('../src/lib/db.js', () => ({
	listConversations: async () => [],
	putConversations: async () => {},
	deleteConversations: async () => {}
}));

// a configuration naming the KiSS server backend as chat agent, with its key
function config(agent: string): ConfigReader {
	const values: Record<string, string> = {
		'chat agent': agent,
		'mcp url backend': 'http://kiss/mcp',
		'mcp key backend': 'k'
	};
	const get = (key: string, name?: string) => values[name ? `${key} ${name}` : key];
	return { get, reveal: get, names: (m) => (m === 'mcp' ? ['backend'] : []) };
}

const conversation = (id: string, title: string, updated: number) => ({
	id,
	title,
	updated,
	leaf: `${id}-a`,
	entries: [
		{ role: 'user', text: 'hi', id: `${id}-u`, parent: null, time: updated },
		{ role: 'assistant', rounds: [], id: `${id}-a`, parent: `${id}-u`, time: updated }
	]
});

// a server whose stream gives the events the test pushes, and the requests
// the page sent it
function server() {
	const sent: { url: string; init?: RequestInit }[] = [];
	let push: (event: object) => void = () => {};
	vi.stubGlobal('fetch', async (url: URL, init?: RequestInit) => {
		sent.push({ url: String(url), init });
		if (String(url).endsWith('/events')) {
			const body = new ReadableStream<Uint8Array>({
				start(c) {
					push = (e) => c.enqueue(new TextEncoder().encode(`data: ${JSON.stringify(e)}\n\n`));
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

afterEach(() => {
	follow(config(''));
	vi.unstubAllGlobals();
});

describe('the conversations of a KiSS server', () => {
	it('follow the stream: every one, each change whole, what a turn grows, the turn, one gone', async () => {
		const s = server();
		app.conversations = [];
		follow(config('backend'));
		await vi.waitFor(() => expect(s.sent[0]?.url).toBe('http://kiss/events'));
		expect(new Headers(s.sent[0].init?.headers).get('authorization')).toBe('Bearer k');
		s.push({
			set: [],
			value: { conversations: [conversation('a', 'A', 1), conversation('b', 'B', 2)] }
		});
		await vi.waitFor(() => expect(app.conversations.map((c) => c.id)).toEqual(['b', 'a']));
		expect(homed('a') && homed('b')).toBe(true);

		s.push({ set: ['a', 'answering'], value: true });
		s.push({ add: ['a', 'entries', 1, 'rounds'], value: { reasoning: '', text: '', calls: [] } });
		s.push({ add: ['a', 'entries', 1, 'rounds', 0, 'text'], text: 'Hel' });
		s.push({ add: ['a', 'entries', 1, 'rounds', 0, 'text'], text: 'lo' });
		await vi.waitFor(() => expect(app.replies.a?.rounds[0]?.text).toBe('Hello'));
		expect(app.replies.a).toBe(app.conversations.find((c) => c.id === 'a')!.entries[1]);

		s.push({ set: ['a', 'answering'], value: null });
		await vi.waitFor(() => expect('a' in app.replies).toBe(false));
		s.push({ set: ['a'], value: { ...conversation('a', 'A2', 3), pinned: true } });
		await vi.waitFor(() =>
			expect(app.conversations[0]).toMatchObject({ id: 'a', title: 'A2', pinned: true })
		);
		s.push({ set: ['b'], value: null });
		await vi.waitFor(() => expect(app.conversations.map((c) => c.id)).toEqual(['a']));
		expect(homed('b')).toBe(false);
	});

	it('ask what a turn of the server asks, the answer going to the server, the question leaving with it', async () => {
		const s = server();
		app.conversations = [];
		app.asks = [];
		follow(config('backend'));
		s.push({ set: [], value: { conversations: [conversation('a', 'A', 1)] } });
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

	it('send the actions of the user to the server, and leave the page with it', async () => {
		const s = server();
		app.conversations = [];
		follow(config('backend'));
		s.push({ set: [], value: { conversations: [conversation('a', 'A', 1)] } });
		await vi.waitFor(() => expect(app.conversations.length).toBe(1));
		expect(await act('send', { text: 'hi' })).toEqual({ id: 'n' });
		const post = s.sent.find((r) => r.url === 'http://kiss/conversations/send')!;
		expect(post.init?.method).toBe('POST');
		expect(JSON.parse(String(post.init?.body))).toEqual({ text: 'hi' });
		expect(homed('n')).toBe(true);
		follow(config(''));
		expect(app.conversations).toEqual([]);
		expect(homed('a')).toBe(false);
	});
});
