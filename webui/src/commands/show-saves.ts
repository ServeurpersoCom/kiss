import type { Command } from '../lib/types.js';
import { beyond, comment, localTime } from '../lib/config.js';
import { listed } from './copy.js';
import { choose } from '../engine/near.js';

export default {
	path: ['show', 'saves'],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (args.length > 1) throw beyond(['show', 'saves', args[0]], args.slice(1));
		return args[0];
	},
	// every save, oldest first; one save by its name as it runs, client.conf
	// included
	run(ctx, word) {
		if (word !== undefined) {
			const name = choose(
				word,
				ctx.archive.list().map((s) => s.name),
				'save'
			);
			const lines = listed(ctx, name);
			return lines.length ? lines.join('\n') : comment('every key is at its default');
		}
		const saves = ctx.archive.list();
		if (!saves.length) return comment('nothing saved yet');
		return saves.map((s) => comment(`${s.name} ${localTime(s.date)}`)).join('\n');
	},
	complete(ctx, args) {
		return args.length ? [] : ctx.archive.list().map((s) => s.name);
	}
} satisfies Command<string | undefined>;
