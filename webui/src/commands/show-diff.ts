import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { beyond, comment } from '../lib/config.js';
import { choose } from '../engine/near.js';
import { configs, effective } from './copy.js';

export default {
	path: ['show', 'diff'],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (args.length < 2) throw new Incomplete();
		if (args.length > 2) throw beyond(['show', 'diff', ...args.slice(0, 2)], args.slice(2));
		return args;
	},
	// from one configuration to another as they run: the running-config, the
	// startup-config, or a save
	run(ctx, words) {
		const [from, to] = words.map((w) => choose(w, configs(ctx), 'configuration'));
		const lines = ctx.schema.diff(effective(ctx, from), effective(ctx, to));
		return lines.length ? lines.join('\n') : comment('no difference');
	},
	complete(ctx, args) {
		return args.length < 2 ? configs(ctx) : [];
	}
} satisfies Command<string[]>;
