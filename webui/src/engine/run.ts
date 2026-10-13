import type {
	ConfigReader,
	Context,
	Key,
	Known,
	Outcome,
	Role,
	Scope,
	Value
} from '../lib/types.js';
import { Incomplete, REFUSE } from '../lib/types.js';
import {
	ERROR_PREFIX,
	FACTORY_FILE,
	REMOVED,
	SITE_CONFIG_URL,
	beyond,
	comment
} from '../lib/config.js';
import factoryText from '../../client.conf?raw';
import { splitFilter, splitLines, tokenize } from './parse.js';
import { FILTER_NAMES, compileFilter } from './filter.js';
import { commands, modules, resolve } from './registry.js';
import { KeySchema, stored, unstore } from './schema.js';
import { Values } from './values.js';
import { SaveArchive } from './archive.js';
import { complete, partialWord } from './complete.js';
import { didYouMean } from './near.js';
import { hold } from '../lib/keychain.js';

const schema = new KeySchema(modules);

// set lines as a layer of stored values, and what they refuse
function layer(text: string, file: string): [Map<string, Value>, string[]] {
	const values = new Map<string, Value>();
	const problems: string[] = [];
	for (const line of splitLines(text)) {
		try {
			const { command, args } = resolve(tokenize(line));
			if (command.path.join(' ') !== 'set') throw new Error(`${file} holds set lines only`);
			const { key, name, rest } = schema.read(args);
			if (!rest.length) throw new Error(`${key} misses its value`);
			values.set(stored(key, name), schema.parse(key, rest.join(' ')));
		} catch (e) {
			problems.push(`${line}: ${(e as Error).message}`);
		}
	}
	return [values, problems];
}

// the factory configuration the page carries, what differs from the reset
// value of a key; a line it refuses or a rule it breaks stops the page at once
const [factory, broken] = layer(factoryText, FACTORY_FILE);
if (broken.length) throw new Error([FACTORY_FILE, ...broken].join('\n'));
// what the site configuration gives, below every save
const site = new Map<string, Value>();
const running = new Values(schema, site, factory);
const archive = new SaveArchive();
const unruly = breaks(running);
if (unruly) throw new Error(`${FACTORY_FILE}: ${unruly}`);

// what replaces a value that must not be kept, and where a value may open
const QUOTE = /["']/;

// the running configuration, read only, for the rest of the page
export const settings: ConfigReader = running;

// the values of every secret the running configuration holds, the ones the
// keychain filters from every answer of the network
function secrets(): string[] {
	const out: string[] = [];
	for (const [key, def] of schema.list()) {
		if (def.kind !== 'secret') continue;
		const names = def.named ? running.names(key.split(' ')[0]) : [undefined];
		for (const name of names) {
			const value = running.reveal(key, name);
			if (value !== undefined) out.push(String(value));
		}
	}
	return out;
}

hold(secrets);

// one compiled line: what runs on the draft, then the filter of its output, if
// any
interface Step {
	run(ctx: Context): Promise<string> | string;
	filter?: (text: string) => string;
}

// a line as the schema alone reads it: the command and the plan of its
// arguments, rights checked, its filter compiled; a lone line that stops short
// lists what may follow it instead
function compile(ctx: Context, text: string, alone: boolean): Step {
	const [head, spec] = splitFilter(text);
	const words = tokenize(head);
	const filter = spec === undefined ? undefined : compileFilter(spec);
	try {
		const { command, args } = resolve(words);
		const path = command.path.join(' ');
		if (!command.roles.includes(ctx.role)) {
			throw new Error(`${path} is reserved to the ${command.roles.join(' and ')}`);
		}
		if (command.alone && !alone) throw new Error(`${path} runs alone, on a line of its own`);
		if (!command.parse && args.length) {
			throw beyond([path], args);
		}
		const plan = command.parse?.(schema, args);
		return { run: (c) => command.run(c, plan), filter };
	} catch (e) {
		if (!(e instanceof Incomplete)) throw e;
		const next = complete(ctx, words, '');
		if (!alone) throw new Error(`"${head}" misses words, it may go on with: ${next.join(' ')}`);
		return { run: () => comment(next.join(' ')), filter };
	}
}

// an item a line names: one the draft already holds, or one its source
// serves, the source read from the draft for the user, who chose every url in
// it, and from the running configuration for the model, whose changes reach
// nothing before they apply; a module that knows no items of its own takes
// any name
async function admit(ctx: Context, module: string, name: string): Promise<void> {
	const m = ctx.modules.find((x) => x.name === module);
	if (!m?.items || ctx.config.names(module).includes(name)) return;
	const source = ctx.role === 'user' ? ctx.config : ctx.running;
	const groups = await m.items({ ...ctx, running: source });
	const known = groups.flatMap((g) => g.names);
	if (known.includes(name)) return;
	const failed = groups.filter((g) => g.error).map((g) => `${g.group}: ${g.error}`);
	throw new Error(
		`unknown ${module} "${name}"` +
			(failed.length ? `, ${failed.join(', ')}` : didYouMean(name, known))
	);
}

// why a configuration breaks the rules of a module, if it does
function breaks(config: ConfigReader): string | null {
	for (const m of modules) {
		const reason = m.validate?.(config, modules);
		if (reason) return `${m.name}: ${reason}`;
	}
	return null;
}

// a layer enters the device as IOS runs a configuration at boot, block by
// block: an item with every key it sets, else a key alone; a block that
// breaks a rule stays out, tried again once others entered, as one item may
// name another; returns each block that stays out and why
function enter(
	values: Map<string, Value>,
	put: (stored: string, value: Value) => void,
	drop: (stored: string) => void
): string[] {
	const blocks = new Map<string, string[]>();
	for (const k of [...values.keys()].sort()) {
		const [key, name] = unstore(k);
		const block = name === undefined ? key : `${key.split(' ')[0]} ${name}`;
		blocks.set(block, [...(blocks.get(block) ?? []), k]);
	}
	const why = new Map<string, string>();
	for (let moved = true; moved;) {
		moved = false;
		for (const [block, keys] of blocks) {
			for (const k of keys) put(k, values.get(k)!);
			const reason = breaks(running);
			if (!reason) {
				blocks.delete(block);
				moved = true;
				continue;
			}
			for (const k of keys) drop(k);
			why.set(block, reason);
		}
	}
	return [...blocks.keys()].map((b) => `${b}: ${why.get(b)}`);
}

// the site configuration: set lines whose values sit between the factory
// configuration and every save, entering line by line then block by block;
// returns each line and block that stays out and why
export function defaults(text: string): string[] {
	const [values, problems] = layer(text, SITE_CONFIG_URL);
	site.clear();
	return [
		...problems,
		...enter(
			values,
			(k, v) => site.set(k, v),
			(k) => site.delete(k)
		)
	];
}

// the startup-config enters the running configuration over client.conf the
// same way, a key no module declares anymore or a value its key no longer
// takes left out; returns each key and block that stays out and why
export function start(): string[] {
	const dropped = running.load(archive.startup());
	const values = new Map(Object.entries(running.values()));
	running.load({});
	const put = (k: string, v: Value) => {
		const [key, name] = unstore(k);
		running.set(key, v, name);
	};
	const drop = (k: string) => running.unset(...unstore(k));
	const problems = [...dropped.map((k) => `${k}: no key takes it`), ...enter(values, put, drop)];
	for (const m of modules) m.apply?.(running);
	return problems;
}

// one batch at a time, whoever sends it: the user or the model
let queue: Promise<unknown> = Promise.resolve();

// a question a batch asks the user: the batch leaves the queue as it stands,
// nothing applied, and asks it outside, so the other batches of the page run
// while the user thinks
class Wait {
	constructor(
		readonly key: string,
		readonly ask: () => Promise<unknown>
	) {}
}

// the questions of a scope as a batch asks them: the answer the user already
// gave to the same question, else a Wait that leaves the batch; and what the
// batch figured once, given back in the same order each time it runs again
function asking(
	scope: Scope,
	answers: Map<string, unknown>,
	figured: unknown[]
): Scope & Pick<Context, 'once'> {
	let made = 0;
	const known = <A extends unknown[], T>(kind: string, ask?: (...args: A) => Promise<T>) =>
		ask &&
		((...args: A): Promise<T> => {
			const key = JSON.stringify([kind, ...args]);
			if (answers.has(key)) return Promise.resolve(answers.get(key) as T);
			throw new Wait(key, () => ask(...args));
		});
	return {
		...scope,
		grant: known('grant', scope.grant),
		secret: known('secret', scope.secret),
		confirm: known('confirm', scope.confirm),
		offer: known('offer', scope.offer),
		pick: known('pick', scope.pick),
		once: <T>(make: () => T): T => {
			if (made === figured.length) figured.push(make());
			return figured[made++] as T;
		}
	};
}

// every line compiles before the first one runs, then runs on a copy of the
// running configuration and of the title of its conversation, which replace
// them only when all of them succeed: a batch applies whole or not at all, and
// answers with what it changed; a command that writes the archive runs alone,
// and a command asks the user before it acts, figuring once what its question
// is about; a batch that asks leaves the queue, then runs again from its first
// line with the answer, on the configuration as it stands then; once the
// signal aborts, the batch neither starts, nor runs another line, nor replaces
// anything; a batch that writes nothing and changes nothing says so
export async function run(text: string, role: Role, scope: Scope = {}): Promise<Outcome> {
	const answers = new Map<string, unknown>();
	const figured: unknown[] = [];
	for (;;) {
		const result = queue.then(() => batch(text, role, asking(scope, answers, figured)));
		queue = result.catch(() => undefined);
		try {
			return await result;
		} catch (e) {
			if (!(e instanceof Wait)) throw e;
			answers.set(e.key, await e.ask());
			scope.signal?.throwIfAborted();
		}
	}
}

async function batch(
	text: string,
	role: Role,
	scope: Scope & Pick<Context, 'once'>
): Promise<Outcome> {
	const { signal, conversation } = scope;
	signal?.throwIfAborted();
	const lines = splitLines(text);
	if (!lines.length) lines.push('');
	const alone = lines.length === 1;
	const draft = running.clone();
	const titled = conversation && { id: conversation.id, title: conversation.title };
	const ctx: Context = {
		...scope,
		role,
		conversation: titled,
		config: draft,
		running,
		archive,
		schema,
		modules,
		commands: commands.filter((c) => c.roles.includes(role)),
		admit: (module, name) => admit(ctx, module, name)
	};
	// the lines of a file compile as lines among others, so none runs alone,
	// then run on the draft; a line that fails names its place in the file
	ctx.lines = async (text) => {
		const steps = splitLines(text).map((line, i) => {
			try {
				return compile(ctx, line, false);
			} catch (e) {
				throw new Error(`file line ${i + 1}: ${(e as Error).message}`);
			}
		});
		const out: string[] = [];
		for (const [i, step] of steps.entries()) {
			let text: string;
			try {
				text = await step.run(ctx);
			} catch (e) {
				if (e instanceof Wait) throw e;
				signal?.throwIfAborted();
				throw new Error(`file line ${i + 1}: ${(e as Error).message}`);
			}
			signal?.throwIfAborted();
			if (step.filter) text = step.filter(text);
			if (text) out.push(text);
		}
		return out.join('\n');
	};
	const fail = (i: number | null, message: string): Outcome => {
		const where = i === null || alone ? '' : `line ${i + 1}: `;
		const out = [ERROR_PREFIX + where + message.replaceAll('\n', '\n' + ERROR_PREFIX)];
		if (!alone) out.push(ERROR_PREFIX + 'nothing applied');
		return { ok: false, text: out.join('\n') };
	};
	const steps: Step[] = [];
	for (const [i, line] of lines.entries()) {
		try {
			steps.push(compile(ctx, line, alone));
		} catch (e) {
			return fail(i, (e as Error).message);
		}
	}
	const out: string[] = [];
	for (const [i, step] of steps.entries()) {
		let text: string;
		try {
			text = await step.run(ctx);
		} catch (e) {
			if (e instanceof Wait) throw e;
			signal?.throwIfAborted();
			return fail(i, (e as Error).message);
		}
		signal?.throwIfAborted();
		if (step.filter) text = step.filter(text) || comment('no line matches');
		if (text) out.push(text);
	}
	const broken: string[] = [];
	for (const m of modules) {
		const reason = m.validate?.(draft, modules);
		if (reason) broken.push(`${m.name}: ${reason}`);
	}
	if (broken.length) return fail(null, broken.join('\n'));
	// the layers below the session are shared, so the registers a batch can
	// move are those the session layer changes
	const changed = draft.changed(running);
	// the servers a rule asks are those of the draft for the user, who chose
	// every url in it, and of the running configuration for the model
	const source = role === 'user' ? draft : running;
	for (const m of modules) {
		if (!m.verify || !changed.some((k) => k.startsWith(m.name + ' '))) continue;
		const reason = await m.verify(draft, running, source).catch((e: Error) => e.message);
		signal?.throwIfAborted();
		if (reason) return fail(null, `${m.name}: ${reason}`);
	}
	const lists = new Map<string, Promise<string[]>>();
	const listed = (module: string) => {
		if (!lists.has(module)) lists.set(module, inventory(ctx, module));
		return lists.get(module)!;
	};
	const before = await state(running, changed, listed);
	const after = await state(draft, changed, listed);
	signal?.throwIfAborted();
	if (role === 'llm') {
		const refused = await guard(draft, before, after, scope);
		if (refused) return fail(null, refused);
	}
	const diff = schema.diff(before, after);
	if (conversation && titled && titled.title !== conversation.title) {
		if (conversation.title) diff.push(`- title ${schema.quote(conversation.title)}`);
		diff.push(`+ title ${schema.quote(titled.title)}`);
		conversation.title = titled.title;
	}
	running.assign(draft);
	for (const m of modules) {
		if (changed.some((k) => k.startsWith(m.name + ' '))) m.apply?.(running);
	}
	return { ok: true, text: [...out, ...diff].join('\n') || comment('no change') };
}

// the items of a module, as its sources list them from the running
// configuration, a source that fails listing none
async function inventory(ctx: Context, module: string): Promise<string[]> {
	const m = modules.find((x) => x.name === module);
	if (!m?.items) return [];
	const groups = await m.items({ ...ctx, running }).catch(() => []);
	return groups.flatMap((g) => g.names);
}

// the registers a set of stored keys names, each with the value in force
// through the layers of a configuration; a register of an item exists while
// some layer names the item or its module lists it
async function state(
	config: Values,
	keys: string[],
	listed: (module: string) => Promise<string[]>
): Promise<Record<string, Value>> {
	const out: Record<string, Value> = {};
	for (const k of keys) {
		const [key, name] = unstore(k);
		const module = key.split(' ')[0];
		if (name !== undefined && !config.names(module).includes(name)) {
			if (!(await listed(module)).includes(name)) continue;
		}
		const value = config.reveal(key, name);
		if (value !== undefined) out[k] = value;
	}
	return out;
}

// the secrets that go with the items a change gives a new url, as the
// question tells them
function along(draft: ConfigReader, to: Record<string, Value | undefined>): string[] {
	return Object.keys(to).flatMap((k) => {
		const [key, name] = unstore(k);
		const module = key.split(' ')[0];
		if (schema.find(key)?.kind !== 'url' || to[k] === undefined) return [];
		return schema
			.list()
			.filter(([s, def]) => def.kind === 'secret' && s.startsWith(module + ' '))
			.filter(([s]) => draft.get(s, name) !== undefined)
			.map(([s]) => comment(`${stored(s, name)} goes with it`));
	});
}

// whether a change opens a way out: a key naming where the page goes, a url
// or a proxy, given a value, an enum moved toward its more open values
function opens(def: Key, from: Value | undefined, to: Value | undefined): boolean {
	if (def.kind !== 'enum') return to !== undefined;
	const values = def.values ?? [];
	return values.indexOf(to ?? '') > values.indexOf(from ?? '');
}

// what the model may change: a guarded register only once the user agrees,
// every time, when the change opens it; returns why the batch stops, if it
// does
async function guard(
	draft: ConfigReader,
	before: Record<string, Value>,
	after: Record<string, Value>,
	scope: Scope
): Promise<string | null> {
	const from: Record<string, Value | undefined> = {};
	const to: Record<string, Value | undefined> = {};
	for (const k of Object.keys(after)) {
		if (before[k] === after[k]) continue;
		const def = schema.find(schema.unstore(k)[0]);
		if (!def?.guard || !opens(def, before[k], after[k])) continue;
		from[k] = before[k];
		to[k] = after[k];
	}
	if (!Object.keys(to).length) return null;
	if (!scope.grant) return 'nobody is here to agree to the change';
	const verdict = await scope.grant({
		kind: 'change',
		lines: [...schema.diff(from, to), ...along(draft, to)]
	});
	scope.signal?.throwIfAborted();
	return verdict === REFUSE ? 'the user refused the change' : null;
}

// the lines as written, the value of every secret they write removed, a line
// that writes none, which asks the user, kept as is; a line that does not
// read keeps what comes before its first quote, where a value may open
export function redact(text: string): string {
	return text
		.split('\n')
		.map((l) => {
			let words: string[];
			try {
				words = tokenize(splitFilter(l)[0]);
			} catch {
				return l.split(QUOTE)[0] + REMOVED;
			}
			try {
				const { command, args } = resolve(words);
				if (command.path.join(' ') !== 'set') return l;
				const { key, def, name, rest } = schema.read(args);
				return def.kind === 'secret' && rest.length ? `set ${stored(key, name)} ${REMOVED}` : l;
			} catch {
				return l;
			}
		})
		.join('\n');
}

// the context a line completes in: the running configuration, the items the
// modules know, and the conversation it is typed in
function completing(role: Role, known: Known, scope: Scope): Context {
	const ctx: Context = {
		...scope,
		role,
		config: running,
		running,
		archive,
		schema,
		modules,
		known,
		once: (make) => make(),
		commands: commands.filter((c) => c.roles.includes(role)),
		admit: (module, name) => admit(ctx, module, name)
	};
	return ctx;
}

// the words that may complete the line, for tab completion: after a pipe the
// filters, else what the command takes
export function suggest(text: string, role: Role, known: Known = {}, scope: Scope = {}): string[] {
	try {
		const partial = partialWord(text);
		const [head, spec] = splitFilter(text.slice(0, text.length - partial.length));
		if (spec !== undefined) {
			const typed = partial.toLowerCase();
			return spec.trim() ? [] : FILTER_NAMES.filter((n) => n.startsWith(typed));
		}
		return complete(completing(role, known, scope), tokenize(head), partial);
	} catch {
		return [];
	}
}

// the items every module knows beyond those set, as their sources list them
// now, a source that fails listing none
export async function known(role: Role): Promise<Known> {
	const ctx = completing(role, {}, {});
	const out: Record<string, string[]> = {};
	await Promise.all(
		modules.map(async (m) => {
			if (!m.items) return;
			const groups = await m.items(ctx).catch(() => []);
			out[m.name] = groups.flatMap((g) => g.names);
		})
	);
	return out;
}
