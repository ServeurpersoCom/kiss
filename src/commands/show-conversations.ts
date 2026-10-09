import type { Command } from '../lib/types.js';
import { ID_SHOWN, comment, localTime } from '../lib/config.js';

export default {
	path: ['show', 'conversations'],
	roles: ['user', 'llm'],
	// newest first, each by its id, its date and its title, the one the batch
	// was sent in marked
	run(ctx) {
		const all = ctx.conversations?.list() ?? [];
		if (!all.length) return comment('no conversation yet');
		return all
			.map((c) => {
				const mark = c.id === ctx.conversation?.id ? '*' : ' ';
				return `${mark} ${c.id.slice(0, ID_SHOWN)} ${localTime(c.updated)} ${ctx.schema.quote(c.title)}`;
			})
			.join('\n');
	}
} satisfies Command;
