import type { Assistant, Conversation, Form, Outcome, Run, Turn } from './types.js';
import type { ConfigReader } from '../engine/engine.js';
import { ATTACHED_STORAGE_KEY } from './config.js';
import { remotes, type Remote } from './remote.js';
import type { Grant, Verdict } from '../engine/engine.js';
import { app, last, pose, withdraw, type Asking } from './app.svelte.js';
import { fill } from './form.js';
import { pulse, type Pulse } from './pulse.js';

// the page attached to a KiSS server, the mcp item backend: it shows the
// conversations of the server alone, follows them by one stream of events and
// never writes one, the server changing them as the actions of the user ask;
// detached, the page holds its own conversations and no server's. The browser
// keeps which of the two attach or detach left; until either runs, a page with
// a backend, the one a KiSS server serves, is attached

// the mcp item the page attaches to
export const BACKEND = 'backend';
const ON = 'on';
const OFF = 'off';

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

// what each conversation of a server asks for its turn, while the turn asks
// it: the number of the question it shows now, and what stops the lines it
// asks this page to run
const questions = new Map<string, { shown: number; stop?: AbortController }>();
// the clock of each turn of a server, by conversation, on the clock of the
// page, read at every frame of the screen
const turns = new Map<string, Pulse>();
// what a moment of the server adds to read on performance.now(), as the
// first event of the stream sets it
let shift = 0;
// the server followed now, by where it goes, with what key and under which
// hostname, whether the page holds its config tool for the server, as the
// first event says, and what ends its stream
let followed: {
	server: Remote;
	key: string;
	client: string;
	holds: boolean;
	stop: AbortController;
} | null = null;

// what the page does for its server: brings its own conversations back once
// it detaches, and runs the lines the model of the server calls its config
// tool with in a conversation
interface Page {
	load(): Promise<void>;
	run(id: string, lines: string, signal: AbortSignal): Promise<Outcome>;
}
let page: Page = {
	load: async () => undefined,
	run: async () => ({ ok: false, text: 'the page runs nothing yet' })
};

// the clock of the turn a conversation of a server answers now, if any
export const turnOf = (id: string): Pulse | undefined => turns.get(id);

// whether the page is attached to its server
export const attached = (): boolean => followed !== null;

export function serve(p: Page): void {
	page = p;
}

// what an event writes: set replaces what its path names, null removing it;
// add appends text to a string or a value to a list
type Event =
	| { set: (string | number)[]; value: unknown }
	| { add: (string | number)[]; text?: string; value?: unknown };

// every conversation leaves the page, with its turn and its question
function forget(): void {
	app.conversations = [];
	app.current = null;
	app.replies = {};
	turns.clear();
	for (const n of questions.values()) (withdraw(n.shown), n.stop?.abort());
	questions.clear();
}

// lines the model of the server asks this page to run, as the config tool of
// the page runs them for a model, what they answer going to the server; the
// run stops once the turn withdraws it; another page runs those it is asked
function run(id: string, request: Run): void {
	const stop = new AbortController();
	questions.set(id, { shown: 0, stop });
	if (!followed?.holds || request.client !== followed.client) return;
	void page
		.run(id, request.lines, stop.signal)
		.then((out) =>
			stop.signal.aborted ? undefined : act('answer', { id, output: out.text, ok: out.ok })
		)
		.catch((e: Error) => console.error(e.message));
}

// what a turn of a server asks the user, a call to grant or a form to fill
// field by field, the answer going to the server; once the turn withdraws
// it, the field it shows leaves and no other comes
function ask(id: string, request: Grant | Form | Run): void {
	if (request.kind === 'run') return run(id, request);
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
		const n = questions.get(id)!;
		withdraw(n.shown);
		n.stop?.abort();
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

// one event applied to the conversations of the server
function apply(e: Event): void {
	if ('set' in e && !e.set.length) {
		const first = e.value as {
			protocol: number;
			client?: string;
			now: number;
			conversations: Conversation[];
		};
		if (first.protocol !== SYNC_PROTOCOL)
			throw new ProtocolError(`speaks protocol ${first.protocol}, this page ${SYNC_PROTOCOL}`);
		shift = performance.now() - first.now;
		if (followed) followed.holds = first.client === followed.client;
		forget();
		const all = first.conversations;
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
			turns.delete(id);
		} else {
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
async function stream(server: Remote, client: string, signal: AbortSignal): Promise<void> {
	const quiet = new AbortController();
	const cut = () => quiet.abort();
	let timer = setTimeout(cut, SYNC_SILENCE_MS);
	signal.addEventListener('abort', cut);
	try {
		const url = new URL(EVENTS, new URL(server.url, location.href));
		url.searchParams.set('client', client);
		const res = await fetch(url, {
			headers: headers(server),
			signal: quiet.signal
		});
		if (!res.ok || !res.body) throw new RefusedError(`${server.url} answers ${res.status}`);
		await read(res.body, () => {
			clearTimeout(timer);
			timer = setTimeout(cut, SYNC_SILENCE_MS);
		});
	} finally {
		clearTimeout(timer);
		signal.removeEventListener('abort', cut);
	}
}

// the events of a body applied as they come, heard told of every chunk
async function read(body: NonNullable<Response['body']>, heard: () => void): Promise<void> {
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
			apply(JSON.parse(data.join('\n')) as Event);
			data = [];
		}
	}
}

// the page attached to its server or detached from it, as the browser keeps
// it, the configuration it follows by
export function attach(config: ConfigReader, on: boolean): void {
	localStorage.setItem(ATTACHED_STORAGE_KEY, on ? ON : OFF);
	follow(config);
}

// the server the page attaches to followed, its stream opened again whenever
// it ends, until the page detaches or the server moves; every conversation
// leaves the page as it attaches or detaches, those of the page coming back
// once it detaches
export function follow(config: ConfigReader): void {
	const backend = remotes(config, 'mcp').find((r) => r.name === BACKEND);
	const kept = localStorage.getItem(ATTACHED_STORAGE_KEY);
	const server = kept === null || kept === ON ? backend : undefined;
	const client = String(config.get('client hostname') ?? '');
	const key = server ? `${server.url} ${JSON.stringify(server.sign())} ${client}` : '';
	if ((followed?.key ?? '') === key) return;
	const was = followed;
	was?.stop.abort();
	followed = null;
	forget();
	if (!server) return void (was && page.load().catch((e: Error) => console.error(e.message)));
	const stop = new AbortController();
	followed = { server, key, client, holds: false, stop };
	void (async () => {
		while (!stop.signal.aborted) {
			// a server that refuses is told; one that speaks another protocol is
			// told and followed no more; a stream cut or silent opens again
			const failed = await stream(server, client, stop.signal).then(
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
// sees; what the server answers: the id of the conversation a line went in, the
// words that may follow a line
export async function act(
	action: string,
	body: object
): Promise<{ id?: string; words?: string[] }> {
	const server = followed?.server;
	if (!server) throw new Error('the page is attached to no server');
	const res = await fetch(new URL(ACTIONS + action, new URL(server.url, location.href)), {
		method: 'POST',
		headers: { ...headers(server), 'Content-Type': 'application/json' },
		body: JSON.stringify(body)
	});
	if (!res.ok) throw new Error((await res.text()).trim() || `${server.url} answers ${res.status}`);
	if (res.status === 204) return {};
	return (await res.json()) as { id?: string; words?: string[] };
}
