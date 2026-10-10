import type { Assistant, Conversation, Grant, Library, Verdict } from './types.js';
import { REFUSE } from './types.js';
import { FILE_EXTENSION, SLASH, TITLE_LENGTH } from './config.js';
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
import { hold } from '../modules/css.js';

// what the page asks the user, and how the answer settles it: a change or a
// call the model asks for, the value of a secret key, a file to save, or a
// file to read
type Asking =
	| { kind: 'grant'; request: Grant; settle(verdict: Verdict): void }
	| { kind: 'secret'; key: string; settle(value: string | null): void }
	| { kind: 'offer'; name: string; text: string; settle(saved: boolean): void }
	| { kind: 'pick'; settle(text: string | null): void }
	| { kind: 'confirm'; question: string; settle(yes: boolean): void };

export const app = $state({
	conversations: [] as Conversation[],
	current: null as Conversation | null,
	// the turn the model writes now, none while it is idle
	reply: null as Assistant | null,
	// one question at a time, as batches and calls run one at a time
	ask: null as Asking | null,
	// the conversation list, open over the thread on a narrow screen
	sidebar: false
});

let controller: AbortController | null = null;
// the conversation the model answers in, empty when it is idle
let answering = '';

// the conversation as it settled, even while the model answers: a call that
// has not run yet never reaches the database; a conversation deleted meanwhile
// stays deleted
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

// out of the list first: no save reaches the database after the delete
export async function remove(ids: readonly string[]): Promise<void> {
	if (ids.includes(answering)) stop();
	app.conversations = app.conversations.filter((c) => !ids.includes(c.id));
	if (app.current && ids.includes(app.current.id)) newChat();
	await deleteConversations(ids);
}

// the type of a conversation file
const FILE_TYPE = 'application/json';

// the browser saves a file; it does so on a gesture of the user only
export function deliver(name: string, text: string): void {
	const url = URL.createObjectURL(new Blob([text], { type: FILE_TYPE }));
	const a = document.createElement('a');
	a.href = url;
	a.download = name;
	a.click();
	URL.revokeObjectURL(url);
}

// the text of a file the user picks, none when they pick none; the browser
// opens its picker on a gesture of the user only
export function choose(): Promise<string | null> {
	return new Promise((resolve) => {
		const input = document.createElement('input');
		input.type = 'file';
		input.accept = FILE_EXTENSION;
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

// the open conversation, created on the first message
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
	const conversation = current(cli ? SLASH + redact(text.slice(SLASH.length)) : text);
	await enter(conversation, conversation.leaf, text);
}

// an entry of the user edited: the line enters as a new version beside it, so
// the model starts over from the very prefix it had, the branch edited kept
export async function edit(id: string, text: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || !entry || app.reply) return;
	await enter(app.current, entry.parent, text);
}

// the model answers a message of the user again, the answer entering as a
// version beside those it had: from the message itself or from an answer to it
export async function retry(id: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || !entry || app.reply) return;
	const user = entry.role === 'assistant' ? entry.parent : entry.role === 'user' ? id : null;
	if (user) await answer(app.current, user);
}

// a slash command leaves the open conversation: the model never reads one, so
// its history stays as it was; a conversation left with no entry goes with it
export async function dismiss(id: string): Promise<void> {
	const entry = app.current?.entries.find((e) => e.id === id);
	if (!app.current || entry?.role !== 'cli' || app.reply) return;
	drop(app.current, id);
	if (app.current.entries.length) await save(app.current);
	else await remove([app.current.id]);
}

// another version shows, as it was last written in
export async function browse(id: string): Promise<void> {
	if (!app.current || app.reply) return;
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
		const result = await run(input, 'user', { ...reach, conversation });
		append(conversation, parent, {
			role: 'cli',
			input: redact(input),
			output: result.text,
			ok: result.ok
		});
		await save(conversation);
		return;
	}
	// a conversation opened by a slash command takes its title from the first message
	if (conversation.title.startsWith(SLASH)) conversation.title = text.slice(0, TITLE_LENGTH);
	const user = append(conversation, parent, { role: 'user', text });
	await answer(conversation, user.id);
}

// the model answers a message of the user from the path up to it, the very
// prefix it read for any answer it gave the message before; the conversation
// dates from that answer, and is saved before the model answers and once it is
// done
async function answer(conversation: Conversation, user: string): Promise<void> {
	conversation.leaf = user;
	const before = path(conversation);
	const entry = append(conversation, user, { role: 'assistant', rounds: [] });
	conversation.updated = entry.time;
	const reply = entry as Assistant;
	app.reply = reply;
	controller = new AbortController();
	answering = conversation.id;
	// the model calls the CLI with its own rights, its batches aborted with the
	// turn and titling the conversation it answers in
	const { signal } = controller;
	const tools = {
		signal,
		cli: (lines: string) => run(lines, 'llm', { ...reach, signal, conversation, grant }),
		redact,
		grant
	};
	try {
		await save(conversation);
		await turn(before, reply, tools, signal);
	} catch (e) {
		reply.error = explain(e as Error);
	} finally {
		app.reply = null;
		controller = null;
		answering = '';
		await save(conversation);
	}
}

// one question to the user at a time, the css sheets held off until it is
// answered
function pose<T>(ask: (settle: (answer: T) => void) => Asking): Promise<T> {
	return new Promise((resolve) => {
		hold(true);
		app.ask = ask((answer) => {
			app.ask = null;
			hold(false);
			resolve(answer);
		});
	});
}

// the user lets the model make a change or a call, or not
const grant = (request: Grant): Promise<Verdict> =>
	pose((settle) => ({ kind: 'grant', request, settle }));

// the value the user gives a secret key, none when they give none
const secret = (key: string): Promise<string | null> =>
	pose((settle) => ({ kind: 'secret', key, settle }));

// whether the user saves the file offered
const offer = (name: string, text: string): Promise<boolean> =>
	pose((settle) => ({ kind: 'offer', name, text, settle }));

// the text of the file the user picks, none when they pick none
const pick = (): Promise<string | null> => pose((settle) => ({ kind: 'pick', settle }));

// whether the user confirms what a command is about to do
const confirm = (question: string): Promise<boolean> =>
	pose((settle) => ({ kind: 'confirm', question, settle }));

// what a batch of the page reaches: the conversations, and the questions it
// may ask the user
const reach = { conversations: library, secret, offer, pick, confirm };

// the turn aborts, and a question it asks is answered no
export function stop(): void {
	controller?.abort();
	const ask = app.ask;
	if (ask?.kind === 'grant') ask.settle(REFUSE);
	else if (ask?.kind === 'offer' || ask?.kind === 'confirm') ask.settle(false);
	else ask?.settle(null);
}
