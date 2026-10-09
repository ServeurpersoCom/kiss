import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { comment } from '../lib/config.js';

export default {
	path: ['load'],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (!args.length) throw new Incomplete();
		if (args.length > 1) throw new Error(`nothing goes after: ${args.slice(1).join(' ')}`);
		return args[0];
	},
	// a save becomes what the session sets
	run(ctx, name) {
		const dropped = ctx.config.load(ctx.archive.find(name).values);
		return dropped.length ? comment(`dropped unknown keys: ${dropped.join(' ')}`) : '';
	},
	complete(ctx, args) {
		return args.length ? [] : ctx.archive.list().map((s) => s.name);
	}
} satisfies Command<string>;
