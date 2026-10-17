// the contracts between the engine and its plugins: a command imports the
// engine, lib/types.ts and lib/config.ts and reaches everything else through
// Context; a module or a tool is where the configuration meets the page, it
// imports what it drives in lib/

import type { Conversation } from '../lib/types.js';

// the user types slash commands in the composer; the model calls its config
// tool
export type Role = 'user' | 'llm';

export type Value = string;

export type Kind = 'string' | 'number' | 'enum' | 'url' | 'secret' | 'css';

// what the user answers when the model asks for a change or a call: this time,
// from now on, or not
export const VERDICTS = ['once', 'always', 'refuse'] as const;
export type Verdict = (typeof VERDICTS)[number];
export const [ONCE, ALWAYS, REFUSE] = VERDICTS;

// what an answer of always does to a request, none for a change, which asks
// every time, and the answers the request takes
export function always(request: Grant): string | null {
	return request.kind === 'call' ? `turns ${request.tool} on` : null;
}

export function answers(request: Grant): readonly Verdict[] {
	return always(request) ? VERDICTS : VERDICTS.filter((v) => v !== ALWAYS);
}

// what the model asks the user to let it do: the lines of a change, or a call
// of a tool with its arguments
export type Grant =
	{ kind: 'change'; lines: string[] } | { kind: 'call'; tool: string; args: string };

export interface Key {
	kind: Kind;
	default?: Value;
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
	// the module whose items the value names, offered when it completes: chat
	// model takes an item of models
	of?: string;
	// a way out of the page, which the model opens only once the user agrees,
	// every time: a url or a string naming where the page goes given a value,
	// an enum moved toward its more open values, which go from the most closed
	// to the most open
	guard?: true;
}

// a module owns the keys spelled after its name: "chat" owns "chat model";
// a key is always named by these two fixed words, data only ever follows them
export interface Module {
	name: string;
	keys: Record<string, Key>;
	// rule across keys, checked on every change, returns a reason when broken
	validate?(config: ConfigReader, modules: readonly Module[]): string | null;
	// rule that asks the servers the configuration names, checked when a batch
	// changes the module, before it applies: before is the configuration it
	// replaces, source the one whose servers are asked; returns a reason when
	// broken
	verify?(config: ConfigReader, before: ConfigReader, source: ConfigReader): Promise<string | null>;
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

// the items each module knows beyond those set, by module, as last listed
export type Known = Readonly<Record<string, readonly string[]>>;

export interface Group {
	group: string;
	names: string[];
	error?: string;
}

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
	// the value in force: the session layer, else the site, else the factory
	// configuration, else the reset value of the key; a secret set reads
	// <removed>
	get(key: string, name?: string): Value | undefined;
	// the value in force, a secret as it is: what the keychain reads as a
	// request leaves, and the engine as it saves, copies or compares
	reveal(key: string, name?: string): Value | undefined;
	// the items of a collection module some layer names
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
	// module declares them anymore or their key no longer takes their value
	load(values: Record<string, Value>): string[];
	// another configuration over the same site and factory layers, what load
	// drops left out
	reader(values: Record<string, Value>): Config;
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
	// what the page starts with over the site file, nothing once erased
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
	// the key the word after a module names, where an item of a collection
	// wins over a prefix of a key that takes an item and has none
	keyAfter(module: string, word: string, followed: boolean): string | undefined;
	collection(module: string): boolean;
	// throws Incomplete when the words stop before the key is named
	read(words: readonly string[]): KeyWords;
	// what may follow the words in a line naming a key, a value too when asked,
	// and right after a collection its items too when asked: the items set, and
	// those known when given; a value takes its current one, a secret aside,
	// and the items of the module it names
	next(
		config: ConfigReader,
		words: readonly string[],
		value: boolean,
		items: boolean,
		known?: Known
	): string[];
	// a value as one token that reads back unchanged
	quote(value: Value): string;
	// the key and the item name of a stored key
	unstore(stored: string): [string, string | undefined];
	// whether a stored key is one a module declares
	known(stored: string): boolean;
	// the line that sets a stored key, a secret with its value
	set(stored: string, value: Value): string;
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
	// asks the user for the value of a secret key, with what tells why when
	// someone else asks, none when they give none
	secret?(key: string, note?: string): Promise<string | null>;
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
	// the site file of the configuration, client.conf or rendering.conf
	site: string;
	schema: Schema;
	modules: readonly Module[];
	// the commands open to the role
	commands: readonly Command<unknown>[];
	// the items the modules know, for completion only
	known?: Known;
	// the lines of a file run as more lines of the batch, on the same draft,
	// whole or not at all with it; their output
	lines?(text: string): Promise<string>;
	// throws unless an item a line names is one its module may hold: one the
	// configuration already holds, or one its source serves, read from the
	// configuration the role reaches; a source that fails says why
	admit(module: string, name: string): Promise<void>;
	// what make gives, figured once for the batch, the n-th call giving back
	// the n-th value when the batch runs again with an answer of the user, so
	// a command figures with it what its question is about
	once<T>(make: () => T): T;
}
