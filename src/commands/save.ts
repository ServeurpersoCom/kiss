import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { NAME_PATTERN, SESSION } from '../lib/config.js';

export default {
	path: ['save'],
	roles: ['user'],
	alone: true,
	parse(_schema, args) {
		if (!args.length) throw new Incomplete();
		if (args.length > 1) throw new Error(`nothing goes after: ${args.slice(1).join(' ')}`);
		if (!NAME_PATTERN.test(args[0]) || args[0] === SESSION) {
			throw new Error(`"${args[0]}" is not a save name`);
		}
		return args[0];
	},
	// the running configuration becomes the latest save under that name, the one
	// the next page load starts with, what the checks find told after it
	async run(ctx, name) {
		const warnings = await ctx.archive.save(ctx.config, name);
		return [`saved ${name}`, ...warnings.map((w) => `! ${w}`)].join('\n');
	},
	complete(ctx, args) {
		return args.length ? [] : [...ctx.archive.list().map((s) => s.name), '<name>'];
	}
} satisfies Command<string>;
