import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { RUNNING_CONFIG, STARTUP_CONFIG, beyond, comment, configuration } from '../lib/config.js';
import { values } from './copy.js';

export default {
	path: ['show', 'diff'],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (args.length < 2) throw new Incomplete();
		if (args.length > 2) throw beyond(['show', 'diff', ...args.slice(0, 2)], args.slice(2));
		return args.map(configuration);
	},
	// from one configuration to another: the running-config, the
	// startup-config, or a save
	run(ctx, [from, to]) {
		const lines = ctx.schema.diff(values(ctx, from), values(ctx, to));
		return lines.length ? lines.join('\n') : comment('no difference');
	},
	complete(ctx, args) {
		return args.length < 2
			? [RUNNING_CONFIG, STARTUP_CONFIG, ...ctx.archive.list().map((s) => s.name)]
			: [];
	}
} satisfies Command<string[]>;
