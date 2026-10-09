import type { Conversation, Message, Round } from './types.js';

// a conversation as a file: a marker naming what it holds, then the
// conversation as the browser stores it, without its id
const MARK = 'kiss';
const KIND = 'conversation';
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

// the messages as they settled, even while the model answers
export function stored(messages: readonly Message[]): Message[] {
	return messages.map((m) => (m.role === 'assistant' ? { ...m, rounds: settled(m.rounds) } : m));
}

export function serialize(c: Conversation): string {
	const file = { [MARK]: KIND, title: c.title, updated: c.updated, messages: stored(c.messages) };
	return JSON.stringify(file, null, INDENT) + '\n';
}

// a check of one value, throwing where it goes wrong
type Check = (value: unknown, at: string) => void;

function fail(at: string, what: string): never {
	throw new Error(`${at}: ${what}`);
}

const string: Check = (v, at) => typeof v === 'string' || fail(at, 'not a string');
const boolean: Check = (v, at) => typeof v === 'boolean' || fail(at, 'not a boolean');
const number: Check = (v, at) => Number.isFinite(v) || fail(at, 'not a number');
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

// a message by its role, each role its own fields
function message(v: unknown, at: string): void {
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
const MESSAGES: Record<string, Check> = {
	user: object({ role: constant('user'), text: string }),
	assistant: object({ role: constant('assistant'), rounds: array(round) }, { error: string }),
	cli: object({ role: constant('cli'), input: string, output: string, ok: boolean })
};
const file = object({
	[MARK]: constant(KIND),
	title: string,
	updated: number,
	messages: array(message)
});

// a conversation file as written by serialize, whole or refused with where it
// goes wrong; the conversation takes a new id
export function parse(text: string): Omit<Conversation, 'id'> {
	let json: unknown;
	try {
		json = JSON.parse(text);
	} catch {
		throw new Error('not JSON');
	}
	file(json, ROOT);
	const { title, updated, messages } = json as Conversation;
	return { title, updated, messages };
}
