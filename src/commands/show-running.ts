import type { Command, Context } from '../lib/types.js';
import { RUNNING_CONFIG, comment } from '../lib/config.js';

// every key not at its default, sorted, as the line that shows it, or with
// secrets the line that sets it, as export running-config writes it
export function running(ctx: Context, secrets = false): string[] {
	return ctx.config.stored().map((k) => {
		const value = ctx.config.get(...ctx.schema.unstore(k))!;
		return secrets ? ctx.schema.set(k, value) : ctx.schema.line(k, value);
	});
}

export default {
	path: ['show', RUNNING_CONFIG],
	roles: ['user', 'llm'],
	run(ctx) {
		const lines = running(ctx);
		return lines.length ? lines.join('\n') : comment('every key is at its default');
	}
} satisfies Command;
