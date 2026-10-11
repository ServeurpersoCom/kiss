import type { Command } from '../lib/types.js';
import { ID_SHOWN, comment, localTime, titled } from '../lib/config.js';

export default {
	path: ['show', 'conversations'],
	roles: ['user', 'llm'],
	// newest first, each by its id, its date and its title when it has one, the
	// one the batch was sent in marked
	run(ctx) {
		const all = ctx.conversations?.list() ?? [];
		if (!all.length) return comment('no conversation yet');
		return all
			.map((c) => {
				const mark = c.id === ctx.conversation?.id ? '*' : ' ';
				const title = titled(c.title, (v) => ctx.schema.quote(v));
				return comment(`${mark} ${c.id.slice(0, ID_SHOWN)} ${localTime(c.updated)}${title}`);
			})
			.join('\n');
	}
} satisfies Command;
