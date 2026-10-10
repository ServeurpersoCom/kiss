import type { Assistant, Conversation, Grant, Library, Verdict } from './types.js';
import { REFUSE } from './types.js';
import { CONFIG_EXTENSION, FILE_EXTENSION, SLASH, TITLE_LENGTH } from './config.js';
import { deleteConversations, listConversations, putConversations } from './db.js';
import { turn } from './agent.js';
import {
	append,
	drop,
	fresh,
	latest,
	named,
	parse,
	path,
	serialize,
	stored
} from './conversation.js';
import { EndpointError } from './api.js';
import { redact, run } from '../engine/run.js';
import { hold } from '../modules/style.js';
import { held, pulse, type Pulse } from './pulse.js';

// what the page asks the user, and how the answer settles it: a change or a
// call the model asks for, the value of a secret key, a file to save, a file
// to read, or a yes
type Asking =
	| { kind: 'grant'; request: Grant; settle(verdict: Verdict): void }
	| { kind: 'secret'; key: string; settle(value: string | null): void }
	| { kind: 'offer'; name: string; text: string; settle(saved: boolean): void }
	| { kind: 'pick'; settle(text: string | null): void }
	| { kind: 'confirm'; question: string; settle(yes: boolean): void };
// a question as it waits for the user, numbered, with the conversation it
// comes from
type Posed = Asking & { id: number; from: string };

export const app = $state({
	conversations: [] as Conversation[],
	current: null as Conversation | null,
	// the turns the model writes now, by conversation
	replies: {} as Record<string, Assistant>,
	// the questions to the user in the order they came, the first one shown,
	// as each is answered in turn
	asks: [] as Posed[],
	// the conversation list, open over the thread on a narrow screen
	sidebar: false
});

// what stops each turn, by conversation
const controllers = new Map<string, AbortController>();
// the clock of each turn, by conversation, read at every frame of the screen
// and never through the state of the page
const pulses = new Map<string, Pulse>();

// the clock of the turn a conversation answers now, if any
export const pulseOf = (id: string): Pulse | undefined => pulses.get(id);

// the conversation as it settled, even while the model answers: a call not
// sent yet never reaches the database, one running reaches it as stopped; a
// conversation deleted meanwhile stays deleted
async function save(conversation: Conversation): Promise<void> {
	if (!app.conversations.some((c) => c.id === conversation.id)) return;
	const snapshot = $state.snapshot(conversation) as Conversation;
	await putConversations([{ ...snapshot, entries: stored(snapshot.entries) }]);
}

// the conversation named by the URL hash, or none
function follow(): void {
	const id = location.hash.slice(1);
	app.current = app.conversations.find((c) => c.id === id) ?? null;
}

// the conversations of the browser, the one the URL names open
export async function restore(): Promise<void> {
	app.conversations = await listConversations();
	follow();
	addEventListener('hashchange', follow);
}

export function open(id: string): void {
	app.sidebar = false;
	location.hash = id;
}

export function newChat(): void {
	app.sidebar = false;
	history.pushState(null, '', location.pathname + location.search);
	app.current = null;
}

// pinned atop the sidebar, or back under its day
export async function pin(id: string): Promise<void> {
	const conversation = app.conversations.find((c) => c.id === id);
	if (!conversation) return;
	if (conversation.pinned) delete conversation.pinned;
	else conversation.pinned = true;
	await save(conversation);
}

// out of the list first: no save reaches the database after the delete; the
// turns of the conversations deleted stop, and their questions are answered no
export async function remove(ids: readonly string[]): Promise<void> {
	for (const id of ids) stop(id);
	app.conversations = app.conversations.filter((c) => !ids.includes(c.id));
	if (app.current && ids.includes(app.current.id)) newChat();
	await deleteConversations(ids);
}

// the browser saves a file, conversations as JSON, a configuration as text; it
// does so on a gesture of the user only
export function deliver(name: string, text: string): void {
	const type = name.endsWith(FILE_EXTENSION) ? 'application/json' : 'text/plain';
	const url = URL.createObjectURL(new Blob([text], { type }));
	const a = document.createElement('a');
	a.href = url;
	a.download = name;
	a.click();
	URL.revokeObjectURL(url);
}

// the text of a file the user picks, conversations or a configuration, none
// when they pick none; the browser opens its picker on a gesture of the user
// only
export function choose(): Promise<string | null> {
	return new Promise((resolve) => {
		const input = document.createElement('input');
		input.type = 'file';
		input.accept = `${FILE_EXTENSION},${CONFIG_EXTENSION}`;
		input.onchange = () => {
			const file = input.files?.[0];
			if (file) file.text().then(resolve, () => resolve(null));
			else resolve(null);
		};
		input.oncancel = () => resolve(null);
		input.click();
	});
}

// the conversations of the page as a batch reaches them; a file unpacks whole,
// those of its conversations already here are skipped, the others written in
// one transaction, never opened: the conversation shown stays as it is
const library: Library = {
	list: () => app.conversations,
	pack: serialize,
	async unpack(text) {
		const found = fresh(parse(text), app.conversations);
		await putConversations(found.added);
		app.conversations = [...found.added, ...app.conversations].sort(
			(a, b) => b.updated - a.updated
		);
		return found;
	},
	find: (prefix) => named(app.conversations, prefix),
	remove
};

// the open conversation, created on the first line, named by a message and
// left without a title by a command
function current(title: string): Conversation {
	if (app.current) return app.current;
	app.conversations.unshift({
		id: crypto.randomUUID(),
		title: title.slice(0, TITLE_LENGTH),
		updated: Date.now(),
		entries: [],
		leaf: null
	});
	const conversation = app.conversations[0];
	app.current = conversation;
	history.pushState(null, '', '#' + conversation.id);
	return conversation;
}

// what went wrong, with the command that fixes it when the endpoint is the cause
function explain(error: Error): string {
	if (error.name === 'AbortError') return 'stopped';
	if (error instanceof EndpointError && error.fix) return `${error.message} -> ${error.fix}`;
	return error.message;
}

// a line typed in the open conversation, after the entry the thread ends on
export async function send(text: string): Promise<void> {
	const cli = text.startsWith(SLASH);
	const conversation = current(cli ? '' : text);
	await enter(conversation, conversation.leaf, text);
}

// an entry of the user edited: the line enters as a new version beside it, so
// the model starts over from the very prefix it had, the branch edited kept; a
// text left as it was enters nothing
export async function edit(id: string, text: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || entry?.role !== 'user' || entry.text === text) return;
	if (app.current.id in app.replies) return;
	await enter(app.current, entry.parent, text);
}

// the model answers a message of the user again, the answer entering as a
// version beside those it had: from the message itself or from an answer to it
export async function retry(id: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || !entry || app.current.id in app.replies) return;
	const user = entry.role === 'assistant' ? entry.parent : entry.role === 'user' ? id : null;
	if (user) await answer(app.current, user);
}

// a slash command leaves the open conversation: the model never reads one, so
// its history stays as it was; a conversation left with no entry goes with it
export async function dismiss(id: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || entry?.role !== 'cli' || app.current.id in app.replies) return;
	drop(app.current, id);
	if (app.current.entries.length) await save(app.current);
	else await remove([app.current.id]);
}

// another version shows, as it was last written in
export async function browse(id: string): Promise<void> {
	if (!app.current || app.current.id in app.replies) return;
	app.current.leaf = latest(app.current, id);
	await save(app.current);
}

// a line entering a conversation after an entry: a line starting with / runs
// on the CLI as the user, anything else goes to the model
async function enter(
	conversation: Conversation,
	parent: string | null,
	text: string
): Promise<void> {
	if (text.startsWith(SLASH)) {
		const input = text.slice(SLASH.length);
		const result = await run(input, 'user', { ...reach(conversation.id), conversation });
		append(conversation, parent, {
			role: 'cli',
			input: redact(input),
			output: result.text,
			ok: result.ok
		});
		await save(conversation);
		return;
	}
	// a conversation with no title yet takes it from its first message
	if (!conversation.title) conversation.title = text.slice(0, TITLE_LENGTH);
	const user = append(conversation, parent, { role: 'user', text });
	await answer(conversation, user.id);
}

// the model answers a message of the user from the path up to it, the very
// prefix it read for any answer it gave the message before; the conversation
// dates from that answer, and is saved before the model answers and once it is
// done; each conversation answers on its own, beside the others
async function answer(conversation: Conversation, user: string): Promise<void> {
	const { id } = conversation;
	conversation.leaf = user;
	const before = path(conversation);
	const entry = append(conversation, user, { role: 'assistant', rounds: [] });
	conversation.updated = entry.time;
	const reply = entry as Assistant;
	const p = pulse(performance.now());
	pulses.set(id, p);
	app.replies[id] = reply;
	const controller = new AbortController();
	controllers.set(id, controller);
	// the model calls the CLI with its own rights, its batches aborted with the
	// turn and titling the conversation it answers in, its questions coming
	// from it
	const { signal } = controller;
	const grant = (request: Grant): Promise<Verdict> =>
		pose(id, (settle) => ({ kind: 'grant', request, settle }));
	const tools = {
		signal,
		cli: (lines: string) => run(lines, 'llm', { ...reach(id), signal, conversation, grant }),
		redact,
		grant,
		keep: () => save(conversation)
	};
	try {
		await save(conversation);
		await turn(before, reply, tools, signal, p);
	} catch (e) {
		reply.error = explain(e as Error);
	} finally {
		delete app.replies[id];
		controllers.delete(id);
		pulses.delete(id);
		await save(conversation);
	}
}

// the number of the last question posed
let posed = 0;

// a question to the user from a conversation, shown once those before it are
// answered; the style tokens and sheets hold off while any question stands,
// and the time it stands leaves the clock of the turn that asks
function pose<T>(from: string, ask: (settle: (answer: T) => void) => Asking): Promise<T> {
	return new Promise((resolve) => {
		const id = ++posed;
		const asked = performance.now();
		hold(true);
		// the first answer settles it, any later one finds it gone
		const settle = (answer: T) => {
			if (!app.asks.some((q) => q.id === id)) return;
			app.asks = app.asks.filter((q) => q.id !== id);
			hold(app.asks.length > 0);
			const p = pulses.get(from);
			if (p) held(p, performance.now() - asked);
			resolve(answer);
		};
		app.asks.push({ ...ask(settle), id, from });
	});
}

// what a batch of the page reaches from a conversation: the conversations, and
// the questions it may ask the user: the value of a secret key, whether the
// file offered is saved, the text of a file picked, whether a command goes on
function reach(from: string) {
	return {
		conversations: library,
		secret: (key: string): Promise<string | null> =>
			pose(from, (settle) => ({ kind: 'secret', key, settle })),
		offer: (name: string, text: string): Promise<boolean> =>
			pose(from, (settle) => ({ kind: 'offer', name, text, settle })),
		pick: (): Promise<string | null> => pose(from, (settle) => ({ kind: 'pick', settle })),
		confirm: (question: string): Promise<boolean> =>
			pose(from, (settle) => ({ kind: 'confirm', question, settle }))
	};
}

// the turn of a conversation aborts, and every question it asks is answered no
export function stop(id: string): void {
	controllers.get(id)?.abort();
	for (const ask of app.asks.filter((q) => q.from === id)) {
		if (ask.kind === 'grant') ask.settle(REFUSE);
		else if (ask.kind === 'offer' || ask.kind === 'confirm') ask.settle(false);
		else ask.settle(null);
	}
}
