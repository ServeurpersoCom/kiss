// contracts between the engine and its plugins: a command imports this file and
// lib/config.ts and reaches everything else through Context; a module or a tool
// is where the configuration meets the page, it imports what it drives in lib/

// the user types slash commands in the composer; the model calls its config
// tool, and the developer terminal runs with exactly the same rights
export type Role = 'user' | 'llm';

export type Value = string;

export type Kind = 'string' | 'number' | 'enum' | 'url' | 'secret';

export interface Key {
	kind: Kind;
	default?: Value;
	// enum choices
	values?: readonly string[];
	// number bounds, both included, and whether it is whole
	min?: number;
	max?: number;
	integer?: boolean;
	// one value per item of a collection, the item named right before the value:
	// set endpoints url prod https://example.com/v1
	named?: boolean;
}

// a module owns the keys spelled after its name: "chat" owns "chat model";
// a key is always named by these two fixed words, data only ever follows them
export interface Module {
	name: string;
	keys: Record<string, Key>;
	// rule across keys, checked on every change, returns a reason when broken
	validate?(config: ConfigReader): string | null;
	// live verification run on every save, resolves to what to warn of, never
	// stopping the save
	check?(config: ConfigReader): Promise<string | null>;
	// mirrors the running configuration into the page after each change
	apply?(config: ConfigReader): void;
	// the items of a collection the module knows beyond those set, by
	// group: who knows them, and why it knows none when it failed
	items?(config: ConfigReader, signal?: AbortSignal): Promise<Group[]>;
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
	// the save the page starts with
	latest(): Save | undefined;
	// saves whatever the module checks find, and resolves to what they warn of;
	// a save of the same name gives way to the new one
	save(config: Config, name: string): Promise<string[]>;
	remove(save: Save): void;
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
	format(key: string, value: Value): string;
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
// in, none from the developer terminal, and the signal of the turn
export interface Scope {
	// aborts the batch of a stopped model turn, and the requests it makes
	signal?: AbortSignal;
	conversation?: Titled;
}

// a conversation as a batch sees it: its title, and nothing of its messages
export interface Titled {
	title: string;
}

// what a command reaches; capabilities extend this interface by module
// augmentation from their own file
export interface Context extends Scope {
	role: Role;
	config: Config;
	archive: Archive;
	schema: Schema;
	modules: readonly Module[];
	// the commands open to the role
	commands: readonly Command<unknown>[];
}

// an image a tool hands back, base64 data, shown to the user only
export interface Image {
	mime: string;
	data: string;
}

// one tool call of a round, with its result once it ran
export interface Call {
	id: string;
	name: string;
	args: string;
	result?: string;
	ok?: boolean;
	images?: Image[];
}

// one request of an assistant turn: what the model streamed back, then the
// calls it made; the turn ends with the first round that calls nothing
export interface Round {
	reasoning: string;
	text: string;
	calls: Call[];
}

// user: what the user typed; assistant: one whole turn, round by round, and
// what stopped it when it failed; cli: a slash command and its output, never
// sent to the model
export type Message =
	| { role: 'user'; text: string }
	| { role: 'assistant'; rounds: Round[]; error?: string }
	| { role: 'cli'; input: string; output: string; ok: boolean };

export type Assistant = Extract<Message, { role: 'assistant' }>;

export interface Conversation {
	id: string;
	title: string;
	updated: number;
	messages: Message[];
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
	// the lines with every secret value masked
	redact(text: string): string;
}

// a tool the model may call: a file of tools/, or one an MCP server serves
export interface Tool {
	name: string;
	description: string;
	parameters: Record<string, unknown>;
	// args, when returned, replace the stored arguments: secrets stay out of the history
	run(ctx: ToolContext, args: Record<string, unknown>): Promise<Outcome & { args?: object }>;
}
