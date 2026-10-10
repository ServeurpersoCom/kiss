import type { Command, Context, Value } from '../lib/types.js';
import { STARTUP_CONFIG, comment } from '../lib/config.js';

// a configuration kept in the archive as the lines that set it, sorted, the
// keys no module declares anymore left out: the startup-config or a save
export function written(ctx: Context, values: Record<string, Value>): string[] {
	return Object.entries(values)
		.filter(([k]) => ctx.schema.known(k))
		.sort(([a], [b]) => a.localeCompare(b))
		.map(([k, v]) => ctx.schema.line(k, v));
}

export default {
	path: ['show', STARTUP_CONFIG],
	roles: ['user', 'llm'],
	// what the page starts with over kiss.conf
	run(ctx) {
		const lines = written(ctx, ctx.archive.startup());
		return lines.length ? lines.join('\n') : comment('the page starts on kiss.conf alone');
	}
} satisfies Command;
