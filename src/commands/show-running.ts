import type { Command } from '../lib/types.js';
import { ALL, RUNNING_CONFIG, beyond, comment } from '../lib/config.js';
import { listed } from './copy.js';
import show from './show.js';

// IOS show running-config: every key not at its default, as the line that
// sets it; all, every key of every module, defaults included, as show
// <module> lists it
export default {
	path: ['show', RUNNING_CONFIG],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (args.length && !ALL.startsWith(args[0].toLowerCase()))
			throw beyond(['show', RUNNING_CONFIG], args);
		if (args.length > 1) throw beyond(['show', RUNNING_CONFIG, ALL], args.slice(1));
		return args.length === 1;
	},
	async run(ctx, all) {
		if (all) {
			const shown = await Promise.all(ctx.modules.map((m) => show.run(ctx, { module: m.name })));
			return shown.join('\n');
		}
		const lines = listed(ctx, RUNNING_CONFIG);
		return lines.length ? lines.join('\n') : comment('every key is at its default');
	},
	complete(_ctx, args) {
		return args.length ? [] : [ALL];
	}
} satisfies Command<boolean>;
