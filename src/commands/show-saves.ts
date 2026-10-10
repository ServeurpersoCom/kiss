import type { Command } from '../lib/types.js';
import { comment, localTime } from '../lib/config.js';

export default {
	path: ['show', 'saves'],
	roles: ['user', 'llm'],
	// oldest first
	run(ctx) {
		const saves = ctx.archive.list();
		if (!saves.length) return comment('nothing saved yet');
		return saves.map((s) => `${s.name} ${localTime(s.date)}`).join('\n');
	}
} satisfies Command;
