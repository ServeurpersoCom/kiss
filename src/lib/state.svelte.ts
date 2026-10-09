import type { Assistant, Conversation, Grant, Message, Verdict } from './types.js';
import { REFUSE } from './types.js';
import { SLASH, TITLE_LENGTH } from './config.js';
import { deleteConversation, listConversations, putConversation } from './db.js';
import { turn } from './agent.js';
import { parse, serialize, stored } from './conversation.js';
import { EndpointError } from './api.js';
import { redact, run } from '../engine/run.js';

// what the page asks the user, and how the answer settles it: a change or a
// call the model asks for, or the value of a secret key
export type Asking =
	| { kind: 'grant'; request: Grant; settle(verdict: Verdict): void }
	| { kind: 'secret'; key: string; settle(value: string | null): void };

export const app = $state({
	conversations: [] as Conversation[],
	current: null as Conversation | null,
	// the turn the model writes now, none while it is idle
	reply: null as Assistant | null,
	// one question at a time, as batches and calls run one at a time
	ask: null as Asking | null,
	terminal: false,
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
	conversation.updated = Date.now();
	const snapshot = $state.snapshot(conversation) as Conversation;
	await putConversation({ ...snapshot, messages: stored(snapshot.messages) });
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

// out of the list first: no save reaches the database after the delete
export async function remove(id: string): Promise<void> {
	if (id === answering) stop();
	app.conversations = app.conversations.filter((c) => c.id !== id);
	if (app.current?.id === id) newChat();
	await deleteConversation(id);
}

// the characters a file name cannot hold, and the type of a conversation file
const UNSAFE_NAME = /[\\/:*?"<>|]/g;
const FILE_TYPE = 'application/json';
export const FILE_EXTENSION = '.json';

// the browser saves a conversation as a file named after its title
export function download(c: Conversation): void {
	const url = URL.createObjectURL(new Blob([serialize(c)], { type: FILE_TYPE }));
	const a = document.createElement('a');
	a.href = url;
	a.download = c.title.replace(UNSAFE_NAME, '_') + FILE_EXTENSION;
	a.click();
	URL.revokeObjectURL(url);
}

// a conversation file becomes a conversation of its own, opened; a file that
// does not read imports nothing and throws where it goes wrong
export async function upload(file: File): Promise<void> {
	const conversation: Conversation = { id: crypto.randomUUID(), ...parse(await file.text()) };
	app.conversations.unshift(conversation);
	await putConversation(conversation);
	open(conversation.id);
}

// the open conversation, created on the first message
function current(title: string): Conversation {
	if (app.current) return app.current;
	app.conversations.unshift({
		id: crypto.randomUUID(),
		title: title.slice(0, TITLE_LENGTH),
		updated: Date.now(),
		messages: []
	});
	const conversation = app.conversations[0];
	app.current = conversation;
	history.pushState(null, '', '#' + conversation.id);
	return conversation;
}

// what went wrong, with the command that fixes it when the endpoint is the cause
function explain(error: Error): string {
	if (error.name === 'AbortError') return 'stopped';
	if (error instanceof EndpointError && error.fix) return `${error.message}. ${error.fix}`;
	return error.message;
}

// a line starting with / runs on the CLI as the user, anything else goes to the
// model; the conversation is saved before the model answers and once it is done
export async function send(text: string): Promise<void> {
	const cli = text.startsWith(SLASH);
	const conversation = current(cli ? SLASH + redact(text.slice(SLASH.length)) : text);
	if (cli) {
		const input = text.slice(SLASH.length);
		const result = await run(input, 'user', { conversation, secret });
		conversation.messages.push({
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
	const before: Message[] = [...conversation.messages, { role: 'user', text }];
	conversation.messages.push(before[before.length - 1], { role: 'assistant', rounds: [] });
	const reply = conversation.messages[conversation.messages.length - 1] as Assistant;
	app.reply = reply;
	controller = new AbortController();
	answering = conversation.id;
	// the model calls the CLI with the rights of the developer terminal, its
	// batches aborted with the turn and titling the conversation it answers in
	const { signal } = controller;
	const tools = {
		signal,
		cli: (lines: string) => run(lines, 'llm', { signal, conversation, grant, secret }),
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

// the user lets the model make a change or a call, or not
function grant(request: Grant): Promise<Verdict> {
	return new Promise((resolve) => {
		app.ask = { kind: 'grant', request, settle: (verdict) => ((app.ask = null), resolve(verdict)) };
	});
}

// the value the user gives a secret key, none when they give none
function secret(key: string): Promise<string | null> {
	return new Promise((resolve) => {
		app.ask = { kind: 'secret', key, settle: (value) => ((app.ask = null), resolve(value)) };
	});
}

// the open conversation cut at a message of the user, then that message sent
// again as written now: the model starts over from the same prefix
export async function edit(index: number, text: string): Promise<void> {
	if (!app.current || app.reply) return;
	app.current.messages.splice(index);
	await send(text);
}

// the turn aborts, and a question it asks is answered no
export function stop(): void {
	controller?.abort();
	if (app.ask?.kind === 'grant') app.ask.settle(REFUSE);
	else app.ask?.settle(null);
}
