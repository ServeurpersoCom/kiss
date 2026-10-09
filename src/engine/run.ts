import type { ConfigReader, Context, Key, Outcome, Role, Scope, Value } from '../lib/types.js';
import { ALLOW, ALWAYS, ASK, DENY, Incomplete, REFUSE } from '../lib/types.js';
import { ERROR_PREFIX, OUTPUT_MAX_LINES, PRIVILEGE, comment } from '../lib/config.js';
import { splitLines, splitPipes, tokenize } from './parse.js';
import { compileFilter } from './filter.js';
import { commands, modules, resolve } from './registry.js';
import { KeySchema, stored } from './schema.js';
import { Values } from './values.js';
import { SaveArchive } from './archive.js';
import { complete, partialWord } from './complete.js';

const schema = new KeySchema(modules);
// what the site configuration gives, below every save
const site = new Map<string, Value>();
const running = new Values(schema, site);
const archive = new SaveArchive(modules);

// what replaces a value that must not be kept, and where a value may open
const MASK = '****';
const QUOTE = /["']/;

// the running configuration, read only, for the rest of the page
export const settings: ConfigReader = running;

// the model reads at most OUTPUT_MAX_LINES lines of one command, so a long
// listing never floods its context
function budget(text: string): string {
	const lines = text.split('\n');
	if (lines.length <= OUTPUT_MAX_LINES) return text;
	const more = lines.length - OUTPUT_MAX_LINES;
	const note = comment(`${more} more lines, narrow with | include`);
	return [...lines.slice(0, OUTPUT_MAX_LINES), note].join('\n');
}

// one compiled line: what runs on the draft, then the filters of its output
interface Step {
	run(ctx: Context): Promise<string> | string;
	filters: ((text: string) => string)[];
}

// a line as the schema alone reads it: the command and the plan of its
// arguments, rights checked, filters compiled; a lone line that stops short
// lists what may follow it instead
function compile(ctx: Context, text: string, alone: boolean): Step {
	const [head, ...pipes] = splitPipes(text);
	const words = tokenize(head);
	const filters = pipes.map(compileFilter);
	try {
		const { command, args } = resolve(words);
		const path = command.path.join(' ');
		if (!command.roles.includes(ctx.role)) {
			throw new Error(`${path} is reserved to the ${command.roles.join(' and ')}`);
		}
		if (command.alone && !alone) throw new Error(`${path} runs alone, on a line of its own`);
		if (!command.parse && args.length) {
			throw new Error(`nothing goes after ${path}: ${args.join(' ')}`);
		}
		const plan = command.parse?.(schema, args);
		return { run: (c) => command.run(c, plan), filters };
	} catch (e) {
		if (!(e instanceof Incomplete)) throw e;
		const next = complete(ctx, words, '');
		if (!alone) throw new Error(`"${head}" misses words, it may go on with: ${next.join(' ')}`);
		return { run: () => next.join('\n'), filters };
	}
}

// the site configuration: set lines whose values sit between the defaults and
// every save, applied whole or not at all like a batch, the rules of every
// module holding; returns what it refuses
export function defaults(text: string): string[] {
	const problems: string[] = [];
	for (const line of splitLines(text)) {
		try {
			const { command, args } = resolve(tokenize(line));
			if (command.path.join(' ') !== 'set') throw new Error('only set lines');
			const { key, name, rest } = schema.read(args);
			if (!rest.length) throw new Error('no value');
			site.set(stored(key, name), schema.parse(key, rest.join(' ')));
		} catch (e) {
			problems.push(`${line}: ${(e as Error).message}`);
		}
	}
	for (const m of modules) {
		const reason = m.validate?.(running, modules);
		if (reason) problems.push(`${m.name}: ${reason}`);
	}
	if (problems.length) site.clear();
	return problems;
}

// the latest save becomes the running configuration
export function start(): void {
	running.load(archive.latest()?.values ?? {});
	for (const m of modules) m.apply?.(running);
}

// one batch at a time, whoever sends it: the user, the model or the terminal
let queue: Promise<unknown> = Promise.resolve();

// every line compiles before the first one runs, then runs on a copy of the
// running configuration and of the title of its conversation, which replace
// them only when all of them succeed: a batch applies whole or not at all, and
// answers with what it changed; a command that writes the archive runs alone;
// once the signal aborts, the batch neither starts, nor runs another line, nor
// replaces anything
export function run(text: string, role: Role, scope: Scope = {}): Promise<Outcome> {
	const result = queue.then(() => batch(text, role, scope));
	queue = result.catch(() => undefined);
	return result;
}

async function batch(text: string, role: Role, scope: Scope): Promise<Outcome> {
	const { signal, conversation } = scope;
	signal?.throwIfAborted();
	const lines = splitLines(text);
	if (!lines.length) lines.push('');
	const alone = lines.length === 1;
	const draft = running.clone(role);
	const titled = conversation && { id: conversation.id, title: conversation.title };
	const ctx: Context = {
		...scope,
		role,
		conversation: titled,
		config: draft,
		archive,
		schema,
		modules,
		commands: commands.filter((c) => c.roles.includes(role))
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
			signal?.throwIfAborted();
			return fail(i, (e as Error).message);
		}
		signal?.throwIfAborted();
		for (const f of step.filters) text = f(text);
		if (step.filters.length && !text) text = comment('no line matches');
		if (text) out.push(role === 'llm' ? budget(text) : text);
	}
	const broken: string[] = [];
	for (const m of modules) {
		const reason = m.validate?.(draft, modules);
		if (reason) broken.push(`${m.name}: ${reason}`);
	}
	if (broken.length) return fail(null, broken.join('\n'));
	if (role === 'llm') {
		const refused = await guard(draft, scope);
		if (refused) return fail(null, refused);
	}
	const [before, after] = resolved(draft);
	const diff = schema.diff(before, after);
	if (conversation && titled && titled.title !== conversation.title) {
		diff.push(`- title ${schema.quote(conversation.title)}`);
		diff.push(`+ title ${schema.quote(titled.title)}`);
		conversation.title = titled.title;
	}
	const changed = draft.changed(running);
	running.assign(draft);
	for (const m of modules) {
		if (changed.some((k) => k.startsWith(m.name + ' '))) m.apply?.(running);
	}
	return { ok: true, text: [...out, ...diff].join('\n') };
}

// the change as the page sees it: every value resolved through the layers,
// before the batch then after it
function resolved(
	draft: Values
): [Record<string, Value | undefined>, Record<string, Value | undefined>] {
	const keys = [...new Set([...running.stored(), ...draft.stored()])];
	const of = (c: Values) => Object.fromEntries(keys.map((k) => [k, c.get(...schema.unstore(k))]));
	return [of(running), of(draft)];
}

// whether a change moves an enum toward its more open values
function opens(def: Key, from: Value | undefined, to: Value | undefined): boolean {
	const values = def.values ?? [];
	return values.indexOf(to ?? '') > values.indexOf(from ?? '');
}

// what the model may change: a guarded key only as far as the privilege of its
// module goes, the user asked when it says ask, an answer of always giving
// those modules allow; privilege, which no privilege rules, stays at the ask of
// its default, and always gives it nothing; returns why the batch stops, if it
// does
async function guard(draft: Values, scope: Scope): Promise<string | null> {
	const [before, after] = resolved(draft);
	const asked = new Set<string>();
	const from: Record<string, Value | undefined> = {};
	const to: Record<string, Value | undefined> = {};
	for (const k of Object.keys(after)) {
		if (before[k] === after[k]) continue;
		const [key] = schema.unstore(k);
		const module = key.split(' ')[0];
		const def = schema.find(key);
		if (!def?.guard || (def.guard === 'opening' && !opens(def, before[k], after[k]))) continue;
		const level = running.get(`${PRIVILEGE} level`, module);
		if (level === DENY) return `the privilege of ${module} denies the change`;
		if (level !== ASK) continue;
		asked.add(module);
		from[k] = before[k];
		to[k] = after[k];
	}
	if (!asked.size) return null;
	if (!scope.grant) return 'nobody is here to agree to the change';
	const allows = [...asked].filter((m) => m !== PRIVILEGE);
	const verdict = await scope.grant({ kind: 'change', lines: schema.diff(from, to), allows });
	scope.signal?.throwIfAborted();
	if (verdict === REFUSE) return 'the user refused the change';
	if (verdict === ALWAYS) for (const m of allows) draft.set(`${PRIVILEGE} level`, ALLOW, m);
	return null;
}

// the lines as written, the value of every secret set by them masked; a line
// that does not read keeps what comes before its first quote, where a value
// may open
export function redact(text: string): string {
	return text
		.split('\n')
		.map((l) => {
			let words: string[];
			try {
				words = tokenize(splitPipes(l)[0]);
			} catch {
				return l.split(QUOTE)[0] + MASK;
			}
			try {
				const { command, args } = resolve(words);
				if (command.path.join(' ') !== 'set') return l;
				const { key, def, name } = schema.read(args);
				return def.kind === 'secret' ? `set ${stored(key, name)} ${MASK}` : l;
			} catch {
				return l;
			}
		})
		.join('\n');
}

// the words that may complete the line, for tab completion
export function suggest(text: string, role: Role): string[] {
	const ctx: Context = {
		role,
		config: running,
		archive,
		schema,
		modules,
		commands: commands.filter((c) => c.roles.includes(role))
	};
	try {
		const partial = partialWord(text);
		return complete(ctx, tokenize(text.slice(0, text.length - partial.length)), partial);
	} catch {
		return [];
	}
}
