import type { Assistant, Conversation, Message } from './types.js';
import { SLASH, TITLE_LENGTH } from './config.js';
import { deleteConversation, listConversations, putConversation } from './db.js';
import { settled, turn } from './agent.js';
import { EndpointError } from './api.js';
import { redact, run } from '../engine/run.js';

export const app = $state({
	conversations: [] as Conversation[],
	current: null as Conversation | null,
	// the turn the model writes now, none while it is idle
	reply: null as Assistant | null,
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
	await putConversation({
		...snapshot,
		messages: snapshot.messages.map((m) =>
			m.role === 'assistant' ? { ...m, rounds: settled(m.rounds) } : m
		)
	});
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
		const result = await run(input, 'user');
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
	// batches aborted with the turn
	const { signal } = controller;
	const tools = { signal, cli: (lines: string) => run(lines, 'llm', signal), redact };
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

export function stop(): void {
	controller?.abort();
}
