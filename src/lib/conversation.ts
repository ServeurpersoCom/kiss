import type { Conversation, Entry, Message, Round } from './types.js';

// conversations as a file: a marker naming what it holds, then the
// conversations as the browser stores them, ids included
const MARK = 'kiss';
const KIND = 'conversations';
const INDENT = '\t';
// the root of a place named in an error
const ROOT = 'file';

// the text a message holds as written, for the clipboard: what the user
// typed, what the model answered without its thinking and calls, what a
// command printed
export function source(m: Message): string {
	switch (m.role) {
		case 'user':
			return m.text;
		case 'assistant':
			return m.rounds
				.map((r) => r.text)
				.filter((text) => text)
				.join('\n\n');
		case 'cli':
			return m.output;
	}
}

// what of a turn holds once it stops or leaves the page: the calls that ran,
// and the rounds that streamed something; a call without an outcome never ran
export function settled(rounds: readonly Round[]): Round[] {
	return rounds
		.map((r) => ({ ...r, calls: r.calls.filter((c) => c.ok !== undefined) }))
		.filter((r) => r.reasoning || r.text || r.calls.length);
}

// the entries as they settled, even while the model answers
export function stored(entries: readonly Entry[]): Entry[] {
	return entries.map((e) => (e.role === 'assistant' ? { ...e, rounds: settled(e.rounds) } : e));
}

// the entries by their id
function index(c: Conversation): Map<string, Entry> {
	return new Map(c.entries.map((e) => [e.id, e]));
}

// a message entering the conversation after an entry, none for a first one; the
// leaf moves to it; returns the entry as the conversation holds it
export function append(c: Conversation, parent: string | null, message: Message): Entry {
	c.entries.push({ ...message, id: crypto.randomUUID(), parent });
	const entry = c.entries[c.entries.length - 1];
	c.leaf = entry.id;
	return entry;
}

// the entries from the first one to the leaf: the thread, and the history the
// model reads
export function path(c: Conversation): Entry[] {
	const byId = index(c);
	const out: Entry[] = [];
	for (let id = c.leaf; id !== null;) {
		const entry = byId.get(id)!;
		out.push(entry);
		id = entry.parent;
	}
	return out.reverse();
}

// the versions of every entry, by the entry they follow, oldest first
export function forks(c: Conversation): Map<string | null, Entry[]> {
	const out = new Map<string | null, Entry[]>();
	for (const e of c.entries) out.set(e.parent, [...(out.get(e.parent) ?? []), e]);
	return out;
}

// the last entry written under an entry, itself included: a branch comes back
// as it was last written in
export function latest(c: Conversation, id: string): string {
	const byId = index(c);
	const under = (e: Entry | undefined): boolean => {
		for (; e; e = e.parent === null ? undefined : byId.get(e.parent)) if (e.id === id) return true;
		return false;
	};
	return [...c.entries].reverse().find(under)!.id;
}

// one conversation or many, as they settled
export function serialize(conversations: readonly Conversation[]): string {
	const file = {
		[MARK]: KIND,
		conversations: conversations.map((c) => ({
			id: c.id,
			title: c.title,
			updated: c.updated,
			leaf: c.leaf,
			entries: stored(c.entries)
		}))
	};
	return JSON.stringify(file, null, INDENT) + '\n';
}

// the conversations of a file whose id is new, and those already here
export function fresh(
	file: readonly Conversation[],
	present: readonly Conversation[]
): { added: Conversation[]; skipped: Conversation[] } {
	const ids = new Set(present.map((c) => c.id));
	return { added: file.filter((c) => !ids.has(c.id)), skipped: file.filter((c) => ids.has(c.id)) };
}

// a check of one value, throwing where it goes wrong
type Check = (value: unknown, at: string) => void;

function fail(at: string, what: string): never {
	throw new Error(`${at}: ${what}`);
}

const string: Check = (v, at) => typeof v === 'string' || fail(at, 'not a string');
const boolean: Check = (v, at) => typeof v === 'boolean' || fail(at, 'not a boolean');
const number: Check = (v, at) => Number.isFinite(v) || fail(at, 'not a number');
const parent: Check = (v, at) =>
	v === null || typeof v === 'string' || fail(at, 'not an id or null');
const constant =
	(value: string): Check =>
	(v, at) =>
		v === value || fail(at, `not ${value}`);

// an object holding exactly its fields, those in optional when it has them
function object(fields: Record<string, Check>, optional: Record<string, Check> = {}): Check {
	return (v, at) => {
		if (!v || typeof v !== 'object' || Array.isArray(v)) fail(at, 'not an object');
		const record = v as Record<string, unknown>;
		for (const key of Object.keys(record)) {
			if (!(key in fields) && !(key in optional)) fail(`${at}.${key}`, 'unknown');
		}
		for (const [key, check] of Object.entries(fields)) {
			if (!(key in record)) fail(`${at}.${key}`, 'missing');
			check(record[key], `${at}.${key}`);
		}
		for (const [key, check] of Object.entries(optional)) {
			if (key in record) check(record[key], `${at}.${key}`);
		}
	};
}

function array(check: Check): Check {
	return (v, at) => {
		if (!Array.isArray(v)) fail(at, 'not an array');
		v.forEach((item, i) => check(item, `${at}[${i}]`));
	};
}

// an entry by its role, each role its own fields beside the link of the entry
function entry(v: unknown, at: string): void {
	const role = (v as { role?: unknown } | null)?.role;
	const variant = MESSAGES[String(role)];
	if (!variant) fail(`${at}.role`, `not one of ${Object.keys(MESSAGES).join(' ')}`);
	variant(v, at);
}

const image = object({ mime: string, data: string });
// a stored call ran: it holds its outcome
const call = object(
	{ id: string, name: string, args: string, result: string, ok: boolean },
	{ images: array(image) }
);
const round = object({ reasoning: string, text: string, calls: array(call) });
const link = { id: string, parent };
const MESSAGES: Record<string, Check> = {
	user: object({ ...link, role: constant('user'), text: string }),
	assistant: object(
		{ ...link, role: constant('assistant'), rounds: array(round) },
		{ error: string }
	),
	cli: object({ ...link, role: constant('cli'), input: string, output: string, ok: boolean })
};
const conversation = object({
	id: string,
	title: string,
	updated: number,
	leaf: parent,
	entries: array(entry)
});
const file = object({ [MARK]: constant(KIND), conversations: array(conversation) });

// a tree: every id once, every parent an entry before its child, so no cycle,
// and a leaf that ends a branch, none only when there is no entry
function tree({ entries, leaf }: Conversation, at: string): void {
	const seen = new Set<string>();
	entries.forEach((e, i) => {
		const where = `${at}.entries[${i}]`;
		if (seen.has(e.id)) fail(`${where}.id`, 'used twice');
		if (e.parent !== null && !seen.has(e.parent))
			fail(`${where}.parent`, 'names no entry before it');
		seen.add(e.id);
	});
	if (leaf === null ? entries.length : !seen.has(leaf)) fail(`${at}.leaf`, 'names no entry');
	if (leaf !== null && entries.some((e) => e.parent === leaf)) fail(`${at}.leaf`, 'ends no branch');
}

// a file as written by serialize, whole or refused with where it goes wrong:
// every conversation once, each a tree
export function parse(text: string): Conversation[] {
	let json: unknown;
	try {
		json = JSON.parse(text);
	} catch {
		throw new Error('not JSON');
	}
	file(json, ROOT);
	const { conversations } = json as { conversations: Conversation[] };
	const seen = new Set<string>();
	conversations.forEach((c, i) => {
		const at = `${ROOT}.conversations[${i}]`;
		if (seen.has(c.id)) fail(`${at}.id`, 'used twice');
		seen.add(c.id);
		tree(c, at);
	});
	return conversations;
}
