import type { Command } from '../lib/types.js';

export default {
	path: ['show', 'running'],
	roles: ['user', 'llm'],
	// every key not at its default, sorted, as the line that sets it
	run(ctx) {
		const lines = ctx.config.stored().map((k) => {
			const value = ctx.config.get(...ctx.schema.unstore(k));
			return value === undefined ? `! ${k} is not set` : ctx.schema.line(k, value);
		});
		return lines.length ? lines.join('\n') : '! every key is at its default';
	}
} satisfies Command;
