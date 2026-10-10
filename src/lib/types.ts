// contracts between the engine and its plugins: a command imports this file and
// lib/config.ts and reaches everything else through Context; a module or a tool
// is where the configuration meets the page, it imports what it drives in lib/

// the user types slash commands in the composer; the model calls its config
// tool
export type Role = 'user' | 'llm';

export type Value = string;

export type Kind = 'string' | 'number' | 'enum' | 'url' | 'secret' | 'css';

// what a change of a key by the model needs from the privilege of its module:
// change, any change; opening, a change toward a later value of an enum whose
// values go from the most closed to the most open
export type Guard = 'change' | 'opening';

// how far the model changes the guarded keys of a module: never, once the user
// agrees, or freely
export const LEVELS = ['deny', 'ask', 'allow'] as const;
export type Level = (typeof LEVELS)[number];
export const [DENY, ASK, ALLOW] = LEVELS;

// what the user answers when the model asks for a change or a call: this time,
// from now on, or not
export const VERDICTS = ['once', 'always', 'refuse'] as const;
export type Verdict = (typeof VERDICTS)[number];
export const [ONCE, ALWAYS, REFUSE] = VERDICTS;

// what an answer of always does to a request, none when it grants nothing,
// and the answers the request takes
export function always(request: Grant): string | null {
	if (request.kind === 'call') return `turns ${request.tool} on`;
	return request.allows.length ? `allows ${request.allows.join(', ')}` : null;
}

export function answers(request: Grant): readonly Verdict[] {
	return always(request) ? VERDICTS : VERDICTS.filter((v) => v !== ALWAYS);
}

// what the model asks the user to let it do: the lines of a change with the
// modules an answer of always allows, none for a change of privilege alone, or
// a call of a tool with its arguments
export type Grant =
	| { kind: 'change'; lines: string[]; allows: string[] }
	| { kind: 'call'; tool: string; args: string };

export interface Key {
	kind: Kind;
	default?: Value;
	// the defaults of some items of a collection, over the default of the key
	defaults?: Record<string, Value>;
	// enum choices
	values?: readonly string[];
	// the property whose values a css value takes: color, width, font-family
	property?: string;
	// number bounds, both included, and whether it is whole
	min?: number;
	max?: number;
	integer?: boolean;
	// one value per item of a collection, the item named right before the value:
	// set endpoints url prod https://example.com/v1
	named?: boolean;
	// the only items it takes, when they are known: a line naming another
	// fails, a value stored under another is dropped as an unknown key is
	names?(modules: readonly Module[]): readonly string[];
	guard?: Guard;
}

// a module owns the keys spelled after its name: "chat" owns "chat model";
// a key is always named by these two fixed words, data only ever follows them
export interface Module {
	name: string;
	keys: Record<string, Key>;
	// rule across keys, checked on every change, returns a reason when broken
	validate?(config: ConfigReader, modules: readonly Module[]): string | null;
	// live verification run on every save, resolves to what to warn of, never
	// stopping the save
	check?(config: ConfigReader): Promise<string | null>;
	// mirrors the running configuration into the page after each change
	apply?(config: ConfigReader): void;
	// the items of a collection the module knows beyond those set, read from
	// the running configuration as anything that reaches the network is, by
	// group: who knows them, none for the one source of the module, and why it
	// knows none when it failed
	items?(ctx: Context): Promise<Group[]>;
}

export interface Group {
	group: string;
	names: string[];
	error?: string;
}

// a command that misses words: the line lists what may follow instead of failing
export class Incomplete extends Error {}

// a command takes its arguments in two steps: parse checks them against the
// schema alone, for every line of a batch before the first one runs; run acts
// on the draft, where everything that depends on state is checked in order
export interface Command<P = void> {
	path: readonly string[];
	roles: readonly Role[];
	// its first argument names a module, so after its path the name of a module
	// competes with the words of longer commands
	module?: true;
	// writes the archive at once, so it runs on a line of its own
	alone?: true;
	// the plan the arguments spell; a command without it takes no argument
	parse?(schema: Schema, args: string[]): P;
	run(ctx: Context, plan: P): Promise<string> | string;
	// candidates for the next argument; a candidate in <> names what to type
	complete?(ctx: Context, args: string[]): string[];
}

// keys are written as typed: "chat model", "endpoints url" with an item name
export interface ConfigReader {
	// the set value, else the site value, else the default of the key
	get(key: string, name?: string): Value | undefined;
	// the items of a collection module, set or given by the site
	names(module: string): string[];
}

export interface Config extends ConfigReader {
	// a value already parsed by the schema
	set(key: string, value: Value, name?: string): void;
	unset(key: string, name?: string): void;
	// drops every key of one item of a collection
	drop(module: string, name: string): void;
	// every stored key set or given by the site, sorted
	stored(): string[];
	// stored keys set in the session only, sorted: what a save keeps
	values(): Record<string, Value>;
	// replaces every key set in the session; returns the keys dropped because no
	// module declares them anymore
	load(values: Record<string, Value>): string[];
}

export interface Save {
	name: string;
	// ISO 8601, UTC
	date: string;
	values: Record<string, Value>;
}

export interface Archive {
	// oldest first
	list(): readonly Save[];
	// throws when no save has that name
	find(name: string): Save;
	// a save of the same name gives way to the new one
	save(name: string, values: Record<string, Value>): void;
	remove(save: Save): void;
	// what the page starts with over kiss.conf, nothing once erased
	startup(): Record<string, Value>;
	boot(values: Record<string, Value> | undefined): void;
}

// the key the leading words of a line name, and the words after it
export interface KeyWords {
	key: string;
	def: Key;
	module: string;
	name?: string;
	rest: string[];
}

export interface Schema {
	find(key: string): Key | undefined;
	// every key with its declaration, sorted
	list(): [string, Key][];
	parse(key: string, raw: string): Value;
	// the module a word names, a prefix being enough
	module(word: string): string;
	// the key of a module a word names, a prefix being enough, if any
	key(module: string, word: string): string | undefined;
	collection(module: string): boolean;
	// throws Incomplete when the words stop before the key is named
	read(words: readonly string[]): KeyWords;
	// what may follow the words in a line naming a key, a value too when asked,
	// and right after a collection its items too when asked
	next(config: ConfigReader, words: readonly string[], value: boolean, items: boolean): string[];
	// a value as one token that reads back unchanged
	quote(value: Value): string;
	// the key and the item name of a stored key
	unstore(stored: string): [string, string | undefined];
	// whether a stored key is one a module declares
	known(stored: string): boolean;
	// one line of show running for a stored key: set, or a comment for a secret
	line(stored: string, value: Value): string;
	// the lines turning one set of stored keys into another, - then +
	diff(from: Record<string, Value | undefined>, to: Record<string, Value | undefined>): string[];
}

// what a batch acts on beyond the configuration: the conversation it was sent
// in, the conversations of the page, the signal of the turn, and the questions
// it may ask the user
export interface Scope {
	// aborts the batch of a stopped model turn, and the requests it makes
	signal?: AbortSignal;
	conversation?: Titled;
	conversations?: Library;
	// asks the user to let the model make a change
	grant?(request: Grant): Promise<Verdict>;
	// asks the user for the value of a secret key, none when they give none
	secret?(key: string): Promise<string | null>;
	// asks the user to confirm what a command is about to do
	confirm?(question: string): Promise<boolean>;
	// offers the user a file to save, resolving to whether they saved it
	offer?(name: string, text: string): Promise<boolean>;
	// asks the user for a file, resolving to its text, none when they pick none
	pick?(): Promise<string | null>;
}

// a conversation as a batch sees it: its id and its title, and nothing of its
// entries
export interface Titled {
	readonly id: string;
	title: string;
}

// the conversations of the page as a batch reaches them: listed, packed into a
// file, and unpacked from one, which adds them; none is ever changed
export interface Library {
	// newest first
	list(): readonly Conversation[];
	pack(conversations: readonly Conversation[]): string;
	// the file read whole, then those of its conversations whose id is new
	// added, those already here skipped
	unpack(text: string): Promise<{ added: Conversation[]; skipped: Conversation[] }>;
	// the one conversation a prefix of its id names
	find(prefix: string): Conversation;
	// those conversations deleted, all of them in one write
	remove(ids: readonly string[]): Promise<void>;
}

// what a command reaches; capabilities extend this interface by module
// augmentation from their own file
export interface Context extends Scope {
	role: Role;
	// the draft the batch writes, which nothing outside the page reads before
	// the batch applies
	config: Config;
	// the configuration as it applies, past the firewall: the one that reaches
	// the network
	running: ConfigReader;
	archive: Archive;
	schema: Schema;
	modules: readonly Module[];
	// the commands open to the role
	commands: readonly Command<unknown>[];
	// the lines of a file run as more lines of the batch, on the same draft,
	// whole or not at all with it; their output
	lines?(text: string): Promise<string>;
	// throws unless an item a line names is one its module may hold: one the
	// configuration already holds, or one its source serves, read from the
	// configuration the role reaches; a source that fails says why
	admit(module: string, name: string): Promise<void>;
}

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
// sent back to that protocol only
export interface Round {
	reasoning: string;
	text: string;
	calls: Call[];
	opaque?: { protocol: string; items: object[] };
}

// what a turn spent, the time the user took on a question left out: its
// tokens, and in milliseconds of the system, generating them and in all
export interface Stats {
	tokens: number;
	generation: number;
	system: number;
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
	// the lines with every secret value masked
	redact(text: string): string;
	// stores the conversation as it settled
	keep(): Promise<void>;
}

// a tool the model may call: a file of tools/, or one an MCP server serves
export interface Tool {
	name: string;
	description: string;
	parameters: Record<string, unknown>;
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
// protocol, the tokens the endpoint counted so far; or that the stream ends,
// or fails with the message of the endpoint, empty when it gives none
export interface Delta {
	content?: string;
	reasoning?: string;
	calls?: { index: number; id?: string; name?: string; args?: string }[];
	opaque?: object;
	usage?: number;
	end?: true;
	error?: string;
}

// how an endpoint speaks: a file of protocols/, named as endpoints protocol
// takes it; the path it posts a request to, the path that lists its models
// and whether a request reads what it lists of its model, the headers that
// carry the key, the parameters of models it never sends, the body of a
// request, the event that ends a stream, and a reader of one stream, which
// tells what the data of each event adds, none for an event that adds nothing
export interface Protocol {
	name: string;
	path: string;
	models: string;
	described: boolean;
	auth(key: string): Record<string, string>;
	drops: readonly string[];
	body(request: Request): object;
	last: string;
	reader(): (data: string) => Delta | undefined;
}
