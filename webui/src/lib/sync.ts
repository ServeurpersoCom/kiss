import type { Assistant, Conversation } from './types.js';
import type { ConfigReader } from '../engine/engine.js';
import { remotes, type Remote } from './remote.js';
import type { Verdict } from '../engine/engine.js';
import { app, last, pose, withdraw } from './app.svelte.js';

// the conversations of the KiSS server chat agent names: the page follows
// them by one stream of events and never writes one, the server alone
// changes them as the actions of the user ask; the page holds them beside its
// own, by the server that holds them

// beside the MCP of the server: the stream, and the path of an action
const EVENTS = 'events';
const ACTIONS = 'conversations/';
const DATA = 'data:';
// the time before a stream that ended opens again
export const SYNC_RETRY_MS = 2000;

// the server each conversation of a server lives on, by conversation id
const homes = new Map<string, string>();
// the question each conversation of a server shows for its turn, by its
// number, while the turn asks it
const questions = new Map<string, number>();
// the server followed now, by where it goes and with what key, and what ends
// its stream
let followed: { server: Remote; key: string; stop: AbortController } | null = null;

// whether the server chat agent names holds a conversation
export const homed = (id: string): boolean => homes.has(id);

// the server chat agent names, none when the page answers itself
export const agent = (): Remote | null => followed?.server ?? null;

// what an event writes: set replaces what its path names, null removing it;
// add appends text to a string or a value to a list
type Event =
	| { set: (string | number)[]; value: unknown }
	| { add: (string | number)[]; text?: string; value?: unknown };

// the conversations of a server leave the page
function forget(server: string): void {
	app.conversations = app.conversations.filter((c) => homes.get(c.id) !== server);
	for (const [id, home] of homes) if (home === server) homes.delete(id);
	for (const [id, n] of questions) {
		if (homes.has(id)) continue;
		withdraw(n);
		questions.delete(id);
	}
	if (app.current && !app.conversations.includes(app.current)) app.current = null;
	for (const id of Object.keys(app.replies))
		if (!app.conversations.some((c) => c.id === id)) delete app.replies[id];
}

// the conversation the page shows, the one the URL names, the answer each
// conversation of a server writes now, as the objects the page holds now, and
// the question its turn asks, which the server takes the answer of
function settle(id: string): void {
	const c = app.conversations.find((x) => x.id === id);
	const request = c?.asking;
	if (request && !questions.has(id)) {
		void pose<Verdict>(id, (answer) => ({ kind: 'grant', request, settle: answer })).then(
			(verdict) => act('answer', { id, verdict }).catch((e: Error) => console.error(e.message))
		);
		questions.set(id, last());
	} else if (!request && questions.has(id)) {
		withdraw(questions.get(id)!);
		questions.delete(id);
	}
	if (location.hash.slice(1) === id) app.current = c ?? null;
	const leaf = c?.entries.find((e) => e.id === c.leaf);
	if (c?.answering && leaf?.role === 'assistant') app.replies[id] = leaf as Assistant;
	else delete app.replies[id];
}

function sorted(): void {
	app.conversations.sort((a, b) => b.updated - a.updated);
}

// one event applied to the conversations of a server
function apply(server: string, e: Event): void {
	if ('set' in e && !e.set.length) {
		forget(server);
		const all = (e.value as { conversations: Conversation[] }).conversations;
		for (const c of all) homes.set(c.id, server);
		app.conversations.push(...all);
		sorted();
		for (const c of all) settle(c.id);
		return;
	}
	const path = 'set' in e ? e.set : e.add;
	const id = String(path[0]);
	if ('set' in e && path.length === 1) {
		const at = app.conversations.findIndex((c) => c.id === id);
		if (e.value === null) {
			if (at >= 0) app.conversations.splice(at, 1);
			homes.delete(id);
		} else {
			homes.set(id, server);
			if (at >= 0) app.conversations[at] = e.value as Conversation;
			else app.conversations.push(e.value as Conversation);
			sorted();
		}
		settle(id);
		return;
	}
	let node: Record<string | number, unknown> | undefined = app.conversations.find(
		(c) => c.id === id
	) as Record<string, unknown> | undefined;
	for (const p of path.slice(1, -1))
		node = node?.[p] as Record<string | number, unknown> | undefined;
	if (!node) return;
	const last = path[path.length - 1];
	if ('set' in e) {
		if (e.value === null) delete node[last];
		else node[last] = e.value;
	} else if (e.text !== undefined) node[last] = String(node[last]) + e.text;
	else (node[last] as unknown[]).push(e.value);
	settle(id);
}

// a server that answers the stream with a refusal, a key wrong or missing
class RefusedError extends Error {}

// what a request to the server carries: its key, then its headers
function headers(server: Remote): Record<string, string> {
	return { ...server.headers, ...server.sign() };
}

// the stream of a server read until it ends, every event applied; data lines
// of an event joined by LF once the blank line that closes it came
async function stream(server: Remote, signal: AbortSignal): Promise<void> {
	const res = await fetch(new URL(EVENTS, new URL(server.url, location.href)), {
		headers: headers(server),
		signal
	});
	if (!res.ok || !res.body) throw new RefusedError(`${server.url} answers ${res.status}`);
	const reader = res.body.pipeThrough(new TextDecoderStream()).getReader();
	let buffer = '';
	let data: string[] = [];
	for (;;) {
		const { value, done } = await reader.read();
		if (done) return;
		buffer += value;
		let nl: number;
		while ((nl = buffer.indexOf('\n')) >= 0) {
			const line = buffer.slice(0, nl).replace(/\r$/, '');
			buffer = buffer.slice(nl + 1);
			if (line.startsWith(DATA)) data.push(line.slice(DATA.length));
			if (line || !data.length) continue;
			apply(server.name, JSON.parse(data.join('\n')) as Event);
			data = [];
		}
	}
}

// the server chat agent names followed, its stream opened again whenever it
// ends, until the configuration names another one or none; the conversations
// of the one followed before leave the page
export function follow(config: ConfigReader): void {
	const name = String(config.get('chat agent') ?? '');
	const server = name ? remotes(config, 'mcp').find((r) => r.name === name) : undefined;
	const key = server ? `${server.url} ${JSON.stringify(server.sign())}` : '';
	if (followed?.key === key) return;
	if (followed) {
		followed.stop.abort();
		forget(followed.server.name);
	}
	followed = null;
	if (!server) return;
	const stop = new AbortController();
	followed = { server, key, stop };
	void (async () => {
		while (!stop.signal.aborted) {
			// a server that refuses is told; a stream cut, as a page that leaves
			// cuts it, opens again
			await stream(server, stop.signal).catch((e: Error) => {
				if (e instanceof RefusedError) console.error(`${EVENTS} of ${server.name}: ${e.message}`);
			});
			if (stop.signal.aborted) return;
			await new Promise((resolve) => setTimeout(resolve, SYNC_RETRY_MS));
		}
	})();
}

// an action of the user, which the server applies and every page following it
// sees; what the server answers, the id of the conversation a line went in
export async function act(action: string, body: object): Promise<{ id?: string }> {
	const server = followed?.server;
	if (!server) throw new Error('chat agent names no server');
	const res = await fetch(new URL(ACTIONS + action, new URL(server.url, location.href)), {
		method: 'POST',
		headers: { ...headers(server), 'Content-Type': 'application/json' },
		body: JSON.stringify(body)
	});
	if (!res.ok) throw new Error((await res.text()).trim() || `${server.url} answers ${res.status}`);
	if (res.status === 204) return {};
	const answer = (await res.json()) as { id?: string };
	if (answer.id) homes.set(answer.id, server.name);
	return answer;
}
