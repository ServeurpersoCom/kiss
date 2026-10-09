import type { Command } from '../lib/types.js';
import { comment } from '../lib/config.js';

export default {
	path: ['show', 'running'],
	roles: ['user', 'llm'],
	// every key not at its default, sorted, as the line that sets it
	run(ctx) {
		const lines = ctx.config
			.stored()
			.map((k) => ctx.schema.line(k, ctx.config.get(...ctx.schema.unstore(k))!));
		return lines.length ? lines.join('\n') : comment('every key is at its default');
	}
} satisfies Command;
