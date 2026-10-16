import type { Command, Context, Value } from '../engine/engine.js';
import { Incomplete } from '../engine/text.js';
import { CONVERSATIONS, NAME_PATTERN } from '../lib/config.js';
import { RUNNING_CONFIG, STARTUP_CONFIG, beyond, comment } from '../engine/text.js';
import { choose, pick } from '../engine/near.js';
import { unstore } from '../engine/schema.js';

// the values a name holds over client.conf: the running configuration, the
// startup-config, or a save
export function values(ctx: Context, name: string): Record<string, Value> {
	if (name === RUNNING_CONFIG) return ctx.config.values();
	if (name === STARTUP_CONFIG) return ctx.archive.startup();
	return ctx.archive.find(name).values;
}

// a configuration as it runs: its values over client.conf, sorted, a key no
// module declares anymore or a value its key no longer takes left out
export function effective(ctx: Context, name: string): Record<string, Value> {
	const config = ctx.config.reader(values(ctx, name));
	return Object.fromEntries(config.stored().map((k) => [k, config.reveal(...unstore(k))!]));
}

// a configuration as the lines that show it, or with secrets the lines that
// set it
export function listed(ctx: Context, name: string, secrets = false): string[] {
	return Object.entries(effective(ctx, name)).map(([k, v]) =>
		secrets ? ctx.schema.set(k, v) : ctx.schema.line(k, v)
	);
}

// the configurations a word may name: the running-config, the
// startup-config, and every save
export function configs(ctx: Context): string[] {
	return [RUNNING_CONFIG, STARTUP_CONFIG, ...ctx.archive.list().map((s) => s.name)];
}

// whether a word names a configuration
export function named(ctx: Context, word: string): boolean {
	return configs(ctx).includes(word);
}

// what erase and export name: the conversations, a configuration, or a
// conversation by its id, a prefix being enough
export function target(ctx: Context, word: string): string {
	const ids = ctx.conversations?.list().map((c) => c.id) ?? [];
	return choose(word, [CONVERSATIONS, ...configs(ctx), ...ids], 'configuration or conversation');
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
	// the source names a configuration, a prefix being enough; the target
	// names the running-config or the startup-config the same way, else a
	// save by its whole name, which it may create
	parse(_schema, args) {
		if (args.length < 2) throw new Incomplete();
		if (args.length > 2) throw beyond(['copy', ...args.slice(0, 2)], args.slice(2));
		const to = pick(args[1], [RUNNING_CONFIG, STARTUP_CONFIG], 'configuration') ?? args[1];
		if (!NAME_PATTERN.test(to)) throw new Error(`"${to}" is not a save name`);
		return [args[0], to];
	},
	// a copy from the running-config tells what the checks find, as a copy
	// of a save keeps what was checked when it was made
	async run(ctx, [word, to]) {
		const from = choose(word, configs(ctx), 'configuration');
		if (from === to) throw new Error('a copy goes from one configuration to another');
		const copied = values(ctx, from);
		if (to === RUNNING_CONFIG) {
			const dropped = ctx.config.load(copied);
			return dropped.length ? comment(`dropped what no key takes: ${dropped.join(', ')}`) : '';
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
		const names = configs(ctx);
		return args.length === 0 ? names : args.length === 1 ? [...names, '<name>'] : [];
	}
} satisfies Command<string[]>;
