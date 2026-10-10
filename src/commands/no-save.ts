import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { beyond, comment } from '../lib/config.js';

export default {
	path: ['no', 'save'],
	roles: ['user'],
	alone: true,
	parse(_schema, args) {
		if (!args.length) throw new Incomplete();
		if (args.length > 1) throw beyond(['no', 'save', args[0]], args.slice(1));
		return args[0];
	},
	run(ctx, name) {
		ctx.archive.remove(ctx.archive.find(name));
		return comment(`deleted the save ${name}`);
	},
	complete(ctx, args) {
		return args.length ? [] : ctx.archive.list().map((s) => s.name);
	}
} satisfies Command<string>;
