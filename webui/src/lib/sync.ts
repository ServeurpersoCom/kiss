import type { Assistant, Conversation, Form, Turn } from './types.js';
import type { ConfigReader } from '../engine/engine.js';
import { remotes, type Remote } from './remote.js';
import type { Grant, Verdict } from '../engine/engine.js';
import { app, last, pose, withdraw, type Asking } from './app.svelte.js';
import { fill } from './form.js';
import { pulse, type Pulse } from './pulse.js';

// the conversations of the KiSS server chat agent names: the page follows
// them by one stream of events and never writes one, the server alone
// changes them as the actions of the user ask; the page holds them beside its
// own, by the server that holds them

// the conversations API of the server: the stream, and the path of an action
const EVENTS = 'conversations/events';
const ACTIONS = 'conversations/';
const DATA = 'data:';
// the protocol of the events this page reads
export const SYNC_PROTOCOL = 1;
// the time before a stream that ended opens again
export const SYNC_RETRY_MS = 2000;
// the silence after which a stream counts as cut: the server writes every
// second, so three seconds without a byte is a connection gone
export const SYNC_SILENCE_MS = 3000;

// the server each conversation of a server lives on, by conversation id
const homes = new Map<string, string>();
// what each conversation of a server asks the user for its turn, while the
// turn asks it: the number of the question it shows now
const questions = new Map<string, { shown: number }>();
// the clock of each turn of a server, by conversation, on the clock of the
// page, read at every frame of the screen
const turns = new Map<string, Pulse>();
// what a moment of the server adds to read on performance.now(), as the
// first event of the stream sets it
let shift = 0;
// the server followed now, by where it goes and with what key, and what ends
// its stream
let followed: { server: Remote; key: string; stop: AbortController } | null = null;

// the clock of the turn a conversation of a server answers now, if any
export const turnOf = (id: string): Pulse | undefined => turns.get(id);

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
	for (const [id, home] of homes) if (home === server) (homes.delete(id), turns.delete(id));
	for (const [id, n] of questions) {
		if (homes.has(id)) continue;
		withdraw(n.shown);
		questions.delete(id);
	}
	if (app.current && !app.conversations.includes(app.current)) app.current = null;
	for (const id of Object.keys(app.replies))
		if (!app.conversations.some((c) => c.id === id)) delete app.replies[id];
}

// what a turn of a server asks the user, a call to grant or a form to fill
// field by field, the answer going to the server; once the turn withdraws
// it, the field it shows leaves and no other comes
function ask(id: string, request: Grant | Form): void {
	const asked = { shown: 0 };
	questions.set(id, asked);
	const posing = <T>(question: (settle: (answer: T) => void) => Asking): Promise<T> => {
		if (questions.get(id) !== asked) return new Promise<T>(() => undefined);
		const answer = pose<T>(id, question);
		asked.shown = last();
		return answer;
	};
	const answered: Promise<object> =
		request.kind === 'form'
			? fill(
					{
						secret: (key, note) => posing((settle) => ({ kind: 'secret', key, note, settle })),
						confirm: (question) => posing((settle) => ({ kind: 'confirm', question, settle }))
					},
					request.server,
					request.input
				).then((response) => ({ id, response }))
			: posing<Verdict>((settle) => ({ kind: 'grant', request, settle })).then((verdict) => ({
					id,
					verdict
				}));
	void answered.then((body) => act('answer', body)).catch((e: Error) => console.error(e.message));
}

// the conversation the page shows, the one the URL names, the answer each
// conversation of a server writes now, as the objects the page holds now, and
// the question its turn asks, which the server takes the answer of
function settle(id: string): void {
	const c = app.conversations.find((x) => x.id === id);
	const request = c?.asking;
	if (request && !questions.has(id)) ask(id, request);
	else if (!request && questions.has(id)) {
		withdraw(questions.get(id)!.shown);
		questions.delete(id);
	}
	if (location.hash.slice(1) === id) app.current = c ?? null;
	const leaf = c?.entries.find((e) => e.id === c.leaf);
	if (c?.turn && leaf?.role === 'assistant') app.replies[id] = leaf as Assistant;
	else delete app.replies[id];
}

// the clock of a turn as the server sets its pulse, one object for the whole
// turn, gone with it
function turned(id: string, t: Turn | null): void {
	if (!t) return void turns.delete(id);
	const at = (moment: number | null): number => (moment === null ? NaN : moment + shift);
	const p = turns.get(id) ?? pulse(0);
	p.round = t.round;
	p.phase = t.phase;
	p.name = t.name;
	p.status = t.status;
	p.attempt = t.attempt;
	p.attempts = t.attempts;
	p.since = at(t.since);
	p.first = at(t.first);
	p.last = at(t.last);
	p.tokens = t.tokens;
	turns.set(id, p);
}

function sorted(): void {
	app.conversations.sort((a, b) => b.updated - a.updated);
}

// one event applied to the conversations of a server
function apply(server: string, e: Event): void {
	if ('set' in e && !e.set.length) {
		const first = e.value as { protocol: number; now: number; conversations: Conversation[] };
		if (first.protocol !== SYNC_PROTOCOL)
			throw new ProtocolError(`speaks protocol ${first.protocol}, this page ${SYNC_PROTOCOL}`);
		shift = performance.now() - first.now;
		forget(server);
		const all = first.conversations;
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
			turns.delete(id);
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
		if (path.length === 2 && last === 'turn') turned(id, e.value as Turn | null);
	} else if (e.text !== undefined) node[last] = String(node[last]) + e.text;
	else (node[last] as unknown[]).push(e.value);
	settle(id);
}

// a server that answers the stream with a refusal, a key wrong or missing,
// and one that speaks a protocol this page does not read, which the page
// stops following
class RefusedError extends Error {}
class ProtocolError extends Error {}

// what a request to the server carries: its key, then its headers
function headers(server: Remote): Record<string, string> {
	return { ...server.headers, ...server.sign() };
}

// the stream of a server read until it ends, or until it stays silent for
// SYNC_SILENCE_MS, every event applied; data lines of an event joined by LF
// once the blank line that closes it came
async function stream(server: Remote, signal: AbortSignal): Promise<void> {
	const quiet = new AbortController();
	const cut = () => quiet.abort();
	let timer = setTimeout(cut, SYNC_SILENCE_MS);
	signal.addEventListener('abort', cut);
	try {
		const res = await fetch(new URL(EVENTS, new URL(server.url, location.href)), {
			headers: headers(server),
			signal: quiet.signal
		});
		if (!res.ok || !res.body) throw new RefusedError(`${server.url} answers ${res.status}`);
		await read(server, res.body, () => {
			clearTimeout(timer);
			timer = setTimeout(cut, SYNC_SILENCE_MS);
		});
	} finally {
		clearTimeout(timer);
		signal.removeEventListener('abort', cut);
	}
}

// the events of a body applied as they come, heard told of every chunk
async function read(
	server: Remote,
	body: NonNullable<Response['body']>,
	heard: () => void
): Promise<void> {
	const reader = body.pipeThrough(new TextDecoderStream()).getReader();
	let buffer = '';
	let data: string[] = [];
	for (;;) {
		const { value, done } = await reader.read();
		if (done) return;
		heard();
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
			// a server that refuses is told; one that speaks another protocol is
			// told and followed no more; a stream cut or silent opens again
			const failed = await stream(server, stop.signal).then(
				() => null,
				(e: Error) => e
			);
			if (failed instanceof RefusedError || failed instanceof ProtocolError)
				console.error(`${EVENTS} of ${server.name}: ${failed.message}`);
			if (stop.signal.aborted || failed instanceof ProtocolError) return;
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
