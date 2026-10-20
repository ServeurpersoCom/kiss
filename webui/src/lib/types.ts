// the contracts of the conversations, the tools and the protocols the agent
// drives

import type { InputRequest } from '@modelcontextprotocol/client';
import type { Grant, Verdict } from '../engine/engine.js';
import type { Phase } from './pulse.js';

// an image a tool hands back, base64 data, shown to the user only
export interface Image {
	mime: string;
	data: string;
}

// one tool call of a round, with its result once it ran; sent, in the page
// only, once it reaches its tool
export interface Call {
	id: string;
	name: string;
	args: string;
	result?: string;
	ok?: boolean;
	images?: Image[];
	sent?: true;
}

// one request of an assistant turn: what the model streamed back, then the
// calls it made; the turn ends with the first round that calls nothing; and
// what the protocol it streamed in needs back that the rest cannot rebuild,
// sent back to that protocol only; a summary holds kept, its text the summary
// of the steps before it but the kept last ones, which the model reads after
// it in their place
export interface Round {
	reasoning: string;
	text: string;
	calls: Call[];
	opaque?: { protocol: string; items: object[] };
	kept?: number;
}

// what a turn spent, the time the user took on a question left out: its
// tokens, and in milliseconds of the system, generating them and in all; and
// the tokens its last request held once answered, when the endpoint told them
export interface Stats {
	tokens: number;
	generation: number;
	system: number;
	context?: number;
}

// user: what the user typed; assistant: one whole turn, round by round, what
// stopped it when it failed, and what it spent once it streamed; cli: a slash
// command and its output, never sent to the model
export type Message =
	| { role: 'user'; text: string }
	| { role: 'assistant'; rounds: Round[]; error?: string; stats?: Stats }
	| { role: 'cli'; input: string; output: string; ok: boolean };

export type Assistant = Extract<Message, { role: 'assistant' }>;

// where a message sits in the tree of its conversation: its own id, the
// entry it follows, none for a first one, and when it entered, in milliseconds
export interface Link {
	id: string;
	parent: string | null;
	time: number;
}

export type Entry = Message & Link;

// the pulse of a turn of a KiSS server as it writes it, its moments on the
// clock of the server, first and last null before the first chunk of the
// round
export interface Turn {
	round: number;
	phase: Phase;
	name: string;
	status: number;
	attempt: number;
	attempts: number;
	since: number;
	first: number | null;
	last: number | null;
	tokens: number;
}

// a form a tool of a KiSS server asks the user to fill in a turn of the
// server, an MCP input request, under the name of the server that asks
export interface Form {
	kind: 'form';
	server: string;
	input: InputRequest;
}

// the lines a call of the model of a KiSS server runs on the config tool of
// the attached pages, every one of them running them
export interface Run {
	kind: 'run';
	lines: string;
}

// a tree of entries, an edit opening a branch beside the entry it edits; the
// path up from the leaf is the thread, and the history the model reads
export interface Conversation {
	id: string;
	title: string;
	// the moment its last answer entered, its making while it has none
	updated: number;
	// kept atop the sidebar, set only when pinned
	pinned?: true;
	entries: Entry[];
	// the entry the thread ends on, none while the conversation is empty
	leaf: string | null;
	// in the page only, a conversation of a KiSS server while a turn of the
	// server answers in it, its pulse, and what that turn asks under its
	// number: a call to grant, a form of one of its tools to fill, or lines an
	// attached page runs
	turn?: Turn;
	asking?: (Grant | Form | Run) & { question: number };
}

export interface Outcome {
	ok: boolean;
	text: string;
	images?: Image[];
}

// what a tool reaches, handed over by the agent for one turn
export interface ToolContext {
	// aborts with the turn
	signal: AbortSignal;
	// runs CLI lines with the rights of the model, aborted with the turn
	cli(text: string): Promise<Outcome>;
	// asks the user to let the model make a call
	grant(request: Grant): Promise<Verdict>;
	// asks the user for the value of a secret, out of the sight of the model,
	// with the note of whoever asks, none when they give none
	secret(key: string, note: string): Promise<string | null>;
	// asks the user to confirm what a server is about to do
	confirm(question: string): Promise<boolean>;
	// the lines with every secret value masked
	redact(text: string): string;
	// stores the conversation as it settled
	keep(): Promise<void>;
}

// a tool the model may call: a file of tools/, or one an MCP server serves
export interface Tool {
	name: string;
	description: string;
	input: Record<string, unknown>;
	// the arguments as the conversation keeps them, before the call is sent:
	// secrets stay out of the history
	stored?(ctx: ToolContext, args: Record<string, unknown>): object;
	run(ctx: ToolContext, args: Record<string, unknown>): Promise<Outcome>;
}

// what one round asks of a model, whatever protocol carries it: the history
// as the conversation keeps it, the tools offered, the parameters set for the
// model by their names in models, numbers as numbers, and what the endpoint
// lists of the model when its protocol reads it
export interface Request {
	model: string;
	system: string;
	messages: readonly Message[];
	tools: readonly Tool[];
	parameters: Record<string, string | number>;
	info?: Record<string, unknown>;
}

// what one event of a stream adds to the round: its thinking, its text, the
// calls by the index the stream gives them, an item the round keeps for its
// protocol, the tokens the endpoint counted so far, the tokens the request
// holds, its prompt and what it generated; or that the stream ends, or fails
// with the message of the endpoint, empty when it gives none
export interface Delta {
	content?: string;
	reasoning?: string;
	calls?: { index: number; id?: string; name?: string; args?: string }[];
	opaque?: object;
	usage?: number;
	context?: number;
	end?: true;
	error?: string;
}

// how an endpoint speaks: a file of protocols/, named as endpoints protocol
// takes it; the path it posts a request to, the path that lists its models
// and whether a request reads what it lists of its model, where it takes a
// key, Header or Header: Scheme, its own headers with those of the endpoint
// over them, the parameters of models it never sends, the body of a
// request, the event that ends a stream, and a reader of one stream, which
// tells what the data of each event adds, none for an event that adds nothing
export interface Protocol {
	name: string;
	path: string;
	models: string;
	described: boolean;
	key: string;
	headers(headers: Record<string, string>): Record<string, string>;
	drops: readonly string[];
	body(request: Request): object;
	last: string;
	reader(): (data: string) => Delta | undefined;
}
