import type { Command, Context } from '../lib/types.js';
import { RUNNING_CONFIG, comment } from '../lib/config.js';

// every key not at its default, sorted, as the line that sets it: what export
// running-config writes to kiss.conf too
export function running(ctx: Context): string[] {
	return ctx.config
		.stored()
		.map((k) => ctx.schema.line(k, ctx.config.get(...ctx.schema.unstore(k))!));
}

export default {
	path: ['show', RUNNING_CONFIG],
	roles: ['user', 'llm'],
	run(ctx) {
		const lines = running(ctx);
		return lines.length ? lines.join('\n') : comment('every key is at its default');
	}
} satisfies Command;
