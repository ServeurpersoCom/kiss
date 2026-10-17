import type { Assistant, Conversation } from './types.js';
import type { Grant, Library, Verdict } from '../engine/engine.js';
import { app, pose as posing, type Asking, type Posed } from './app.svelte.js';
import { REFUSE } from '../engine/engine.js';
import { CONFIG_EXTENSION, FILE_EXTENSION, SLASH, TITLE_LENGTH, capital } from './config.js';
import { deleteConversations, listConversations, putConversations } from './db.js';
import { prune } from './drafts.js';
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
	stored,
	uuid
} from './conversation.js';
import { EndpointError } from './api.js';
import { locked } from './mcp.js';
import { redact, run, settings } from '../engine/run.js';
import { held, pulse, type Pulse } from './pulse.js';
import { PAGE_COMMANDS, act, attached, detaching, turnOf } from './sync.js';
import { resolve } from '../engine/registry.js';

export { app };

// what stops each turn, by conversation
const controllers = new Map<string, AbortController>();
// the clock of each turn, by conversation, read at every frame of the screen
// and never through the state of the page
const pulses = new Map<string, Pulse>();

// the clock of the turn a conversation answers now, on the page or on its
// server, if any
export const pulseOf = (id: string): Pulse | undefined => pulses.get(id) ?? turnOf(id);

// what asks the user from the page itself, in no conversation: the key of a
// server the page reaches, a command the page runs while attached
const PAGE = '';

// the question a conversation shows, the first it or the page asks, if any
export const askOf = (id: string | undefined): Posed | undefined =>
	app.asks.find((q) => q.from === id || q.from === PAGE);

// the conversation as it settled, even while the model answers: a call not
// sent yet never reaches the database, one running reaches it as stopped; a
// conversation deleted meanwhile stays deleted, and an attached page writes
// none: the server writes its own
async function save(conversation: Conversation): Promise<void> {
	if (attached() || !app.conversations.some((c) => c.id === conversation.id)) return;
	const snapshot = $state.snapshot(conversation) as Conversation;
	await putConversations([{ ...snapshot, entries: stored(snapshot.entries) }]);
}

// the conversation named by the URL hash, or none
function follow(): void {
	const id = location.hash.slice(1);
	app.current = app.conversations.find((c) => c.id === id) ?? null;
}

// the conversations of the browser beside those opened while they load, the
// one the URL names open
async function load(): Promise<void> {
	const stored = await listConversations();
	const opened = app.conversations;
	app.conversations = [...opened, ...stored.filter((c) => !opened.some((o) => o.id === c.id))].sort(
		(a, b) => b.updated - a.updated
	);
	prune(app.conversations.map((c) => c.id));
	follow();
}

// a detached page shows the conversations of the browser, now and each time
// it detaches; an attached one shows those of its server as they come
export async function restore(): Promise<void> {
	detaching(load);
	if (!attached()) await load();
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
	if (attached()) return void (await act('pin', { id, pinned: !conversation.pinned }));
	if (conversation.pinned) delete conversation.pinned;
	else conversation.pinned = true;
	await save(conversation);
}

// out of the list first: no save reaches the database after the delete; the
// turns of the conversations deleted stop, their questions are answered no, and
// their drafts go
export async function remove(ids: readonly string[]): Promise<void> {
	if (attached()) return void (await act('erase', { ids }));
	for (const id of ids) stop(id);
	app.conversations = app.conversations.filter((c) => !ids.includes(c.id));
	prune(app.conversations.map((c) => c.id));
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
		id: uuid(),
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

// what went wrong, with the command that fixes it when the endpoint is the
// cause, as the page tells it, its first letter capital
function explain(error: Error): string {
	if (error.name === 'AbortError') return 'Stopped';
	if (error instanceof EndpointError && error.fix)
		return capital(`${error.message} -> ${error.fix}`);
	return capital(error.message);
}

// a line typed in the open conversation, after the entry the thread ends on;
// whether it went through
export async function send(text: string): Promise<boolean> {
	if (attached()) return remote(text);
	const conversation = current(text.startsWith(SLASH) ? '' : text);
	return enter(conversation, conversation.leaf, text);
}

// whether a CLI line is one an attached page runs itself
function own(input: string): boolean {
	try {
		return PAGE_COMMANDS.includes(resolve(input.trim().split(/\s+/)).command.path.join(' '));
	} catch {
		return false;
	}
}

// a line the page runs itself, in no conversation, what it says going to the
// console; whether it went through
async function page(input: string): Promise<boolean> {
	const result = await run(input, 'user', reach(PAGE));
	if (!result.ok) console.error(result.text);
	return result.ok;
}

// a line of an attached page, in the open conversation of the server or
// opening one: a message goes to its agent, a command runs on the server,
// except one the page runs itself; whether it went through
async function remote(text: string): Promise<boolean> {
	const id = app.current?.id;
	const cli = text.startsWith(SLASH);
	const input = text.slice(SLASH.length);
	if (cli && own(input)) return page(input);
	try {
		const body = cli ? { input } : { text };
		const sent = await act(cli ? 'command' : 'send', id ? { id, ...body } : body);
		if (!id && sent.id) open(sent.id);
		return true;
	} catch (e) {
		console.error((e as Error).message);
		return false;
	}
}

// every MCP server that takes no request without the right key asks the user
// for it, as the line set mcp key <name> does, again while the key is wrong,
// until the user gives none; the line runs on the page, in the open
// conversation of a detached page, in none on an attached one
export async function unlock(): Promise<void> {
	const given = new Set<string>();
	for (;;) {
		const name = (await locked(settings)).find((n) => !given.has(n));
		if (name === undefined) return;
		const line = `set mcp key ${name}`;
		if (!(await (attached() ? page(line) : send(SLASH + line)))) given.add(name);
	}
}

// an entry of the user edited: the line enters as a new version beside it, so
// the model starts over from the very prefix it had, the branch edited kept; a
// text left as it was enters nothing
export async function edit(id: string, text: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || entry?.role !== 'user' || entry.text === text) return;
	if (app.current.id in app.replies) return;
	if (attached()) return void (await act('edit', { id: app.current.id, entry: id, text }));
	await enter(app.current, entry.parent, text);
}

// the model answers a message of the user again, the answer entering as a
// version beside those it had: from the message itself or from an answer to it
export async function retry(id: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || !entry || app.current.id in app.replies) return;
	if (attached()) return void (await act('retry', { id: app.current.id, entry: id }));
	const user = entry.role === 'assistant' ? entry.parent : entry.role === 'user' ? id : null;
	if (user) await answer(app.current, user);
}

// a slash command leaves the open conversation: the model never reads one, so
// its history stays as it was, even while it answers; a conversation left with
// no entry goes with it
export async function dismiss(id: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || entry?.role !== 'cli') return;
	if (attached()) return void (await act('drop', { id: app.current.id, entry: id }));
	drop(app.current, id);
	if (app.current.entries.length) await save(app.current);
	else await remove([app.current.id]);
}

// another version shows, as it was last written in
export async function browse(id: string): Promise<void> {
	if (!app.current || app.current.id in app.replies) return;
	if (attached()) return void (await act('browse', { id: app.current.id, entry: id }));
	app.current.leaf = latest(app.current, id);
	await save(app.current);
}

// a line entering a conversation after an entry: a line starting with / runs
// on the CLI as the user, anything else goes to the model
async function enter(
	conversation: Conversation,
	parent: string | null,
	text: string
): Promise<boolean> {
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
		return result.ok;
	}
	// a conversation with no title yet takes it from its first message
	if (!conversation.title) conversation.title = text.slice(0, TITLE_LENGTH);
	const user = append(conversation, parent, { role: 'user', text });
	await answer(conversation, user.id);
	return true;
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
		secret: reach(id).secret,
		confirm: reach(id).confirm,
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

// a question to the user from a conversation; the time it stands leaves the
// clock of the turn that asks
function pose<T>(from: string, ask: (settle: (answer: T) => void) => Asking): Promise<T> {
	return posing(from, ask, (ms) => {
		const p = pulses.get(from);
		if (p) held(p, ms);
	});
}

// what a batch of the page reaches from a conversation: the conversations, and
// the questions it may ask the user: the value of a secret key, whether the
// file offered is saved, the text of a file picked, whether a command goes on
function reach(from: string) {
	return {
		conversations: library,
		secret: (key: string, note?: string): Promise<string | null> =>
			pose(from, (settle) => ({ kind: 'secret', key, note, settle })),
		offer: (name: string, text: string): Promise<boolean> =>
			pose(from, (settle) => ({ kind: 'offer', name, text, settle })),
		pick: (): Promise<string | null> => pose(from, (settle) => ({ kind: 'pick', settle })),
		confirm: (question: string): Promise<boolean> =>
			pose(from, (settle) => ({ kind: 'confirm', question, settle }))
	};
}

// the turn of a conversation aborts, and every question it asks is answered no
export function stop(id: string): void {
	if (attached()) void act('stop', { id });
	controllers.get(id)?.abort();
	for (const ask of app.asks.filter((q) => q.from === id)) {
		if (ask.kind === 'grant') ask.settle(REFUSE);
		else if (ask.kind === 'offer' || ask.kind === 'confirm') ask.settle(false);
		else ask.settle(null);
	}
}
