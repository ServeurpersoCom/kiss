import type { Command, Context, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import {
	NAME_PATTERN,
	RUNNING_CONFIG,
	STARTUP_CONFIG,
	beyond,
	comment,
	configuration
} from '../lib/config.js';

// the values a name holds over kiss.conf: the running configuration, the
// startup-config, or a save
export function values(ctx: Context, name: string): Record<string, Value> {
	if (name === RUNNING_CONFIG) return ctx.config.values();
	if (name === STARTUP_CONFIG) return ctx.archive.startup();
	return ctx.archive.find(name).values;
}

// whether a word names a configuration: the running-config, the
// startup-config, or a save
export function named(ctx: Context, word: string): boolean {
	return (
		word === RUNNING_CONFIG ||
		word === STARTUP_CONFIG ||
		ctx.archive.list().some((s) => s.name === word)
	);
}

// what the checks of the modules find in the running configuration
async function checks(ctx: Context): Promise<string[]> {
	const found = await Promise.all(
		ctx.modules.map((m) => (m.check ? m.check(ctx.config).catch((e: Error) => e.message) : null))
	);
	return found.filter((f) => f !== null);
}

// IOS copy, from a configuration to another: the running-config, the
// startup-config the page starts with, or a save by its name; the model writes
// the running-config under the firewall as any change is, another once the
// user confirms it
export default {
	path: ['copy'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (args.length < 2) throw new Incomplete();
		if (args.length > 2) throw beyond(['copy', ...args.slice(0, 2)], args.slice(2));
		const [from, to] = args.map(configuration);
		if (!NAME_PATTERN.test(to)) throw new Error(`"${to}" is not a save name`);
		if (from === to) throw new Error('a copy goes from one configuration to another');
		return [from, to];
	},
	// a copy from the running-config tells what the checks find, as a copy
	// of a save keeps what was checked when it was made
	async run(ctx, [from, to]) {
		const copied = values(ctx, from);
		if (to === RUNNING_CONFIG) {
			const dropped = ctx.config.load(copied);
			return dropped.length ? comment(`dropped unknown keys: ${dropped.join(' ')}`) : '';
		}
		if (ctx.role === 'llm') {
			if (!ctx.confirm) throw new Error('nobody is here to confirm the copy');
			const replaced = ctx.archive.list().some((s) => s.name === to) ? ', replacing it' : '';
			const yes = await ctx.confirm(`Copy ${from} to ${to}${replaced}?`);
			ctx.signal?.throwIfAborted();
			if (!yes) throw new Error('the user copied nothing');
		}
		const warnings = from === RUNNING_CONFIG ? await checks(ctx) : [];
		if (to === STARTUP_CONFIG) ctx.archive.boot(copied);
		else ctx.archive.save(to, copied);
		return [`copied ${from} to ${to}`, ...warnings].map(comment).join('\n');
	},
	complete(ctx, args) {
		const names = [RUNNING_CONFIG, STARTUP_CONFIG, ...ctx.archive.list().map((s) => s.name)];
		return args.length === 0 ? names : args.length === 1 ? [...names, '<name>'] : [];
	}
} satisfies Command<string[]>;
