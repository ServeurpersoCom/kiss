import type { ConfigReader, Context, Role, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { ERROR_PREFIX, OUTPUT_MAX_LINES } from '../lib/config.js';
import { splitLines, splitPipes, tokenize } from './parse.js';
import { compileFilter } from './filter.js';
import { commands, modules, resolve } from './registry.js';
import { KeySchema, stored } from './schema.js';
import { Values } from './values.js';
import { SaveArchive } from './archive.js';
import { complete, partialWord } from './complete.js';

export interface Result {
	ok: boolean;
	text: string;
}

const schema = new KeySchema(modules);
// what the site configuration gives, below every save
const site = new Map<string, Value>();
const running = new Values(schema, site);
const archive = new SaveArchive(modules);

// the running configuration, read only, for the rest of the page
export const settings: ConfigReader = running;

// the model reads at most OUTPUT_MAX_LINES lines of one command, so a long
// listing never floods its context
function budget(text: string): string {
	const lines = text.split('\n');
	if (lines.length <= OUTPUT_MAX_LINES) return text;
	const more = lines.length - OUTPUT_MAX_LINES;
	return [...lines.slice(0, OUTPUT_MAX_LINES), `! ${more} more lines, narrow with | include`].join(
		'\n'
	);
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
// every save; returns the lines it refuses
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
// running configuration, which replaces it only when all of them succeed: a
// batch applies whole or not at all, and answers with what it changed; a
// command that writes the archive runs alone; once the signal aborts, the
// batch neither starts, nor runs another line, nor replaces anything
export function run(text: string, role: Role, signal?: AbortSignal): Promise<Result> {
	const result = queue.then(() => batch(text, role, signal));
	queue = result.catch(() => undefined);
	return result;
}

async function batch(text: string, role: Role, signal?: AbortSignal): Promise<Result> {
	signal?.throwIfAborted();
	const lines = splitLines(text);
	if (!lines.length) lines.push('');
	const alone = lines.length === 1;
	const draft = running.clone(role);
	const ctx: Context = {
		role,
		signal,
		config: draft,
		archive,
		schema,
		modules,
		commands: commands.filter((c) => c.roles.includes(role))
	};
	const fail = (i: number | null, message: string): Result => {
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
		if (step.filters.length && !text) text = '! no line matches';
		if (text) out.push(role === 'llm' ? budget(text) : text);
	}
	const broken: string[] = [];
	for (const m of modules) {
		const reason = m.validate?.(draft);
		if (reason) broken.push(`${m.name}: ${reason}`);
	}
	if (broken.length) return fail(null, broken.join('\n'));
	// the change as the page sees it: every value resolved through the layers
	const keys = [...new Set([...running.stored(), ...draft.stored()])];
	const resolved = (c: Values) =>
		Object.fromEntries(keys.map((k) => [k, c.get(...schema.unstore(k))]));
	const diff = schema.diff(resolved(running), resolved(draft));
	const changed = draft.changed(running);
	running.assign(draft);
	for (const m of modules) {
		if (changed.some((k) => k.startsWith(m.name + ' '))) m.apply?.(running);
	}
	return { ok: true, text: [...out, ...diff].join('\n') };
}

// the lines as written, the value of every secret set by them masked
export function redact(text: string): string {
	return text
		.split('\n')
		.map((l) => {
			try {
				const { command, args } = resolve(tokenize(splitPipes(l)[0]));
				if (command.path.join(' ') !== 'set') return l;
				const { key, def, name } = schema.read(args);
				return def.kind === 'secret' ? `set ${stored(key, name)} ****` : l;
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
