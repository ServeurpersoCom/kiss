import type { Command } from '../lib/types.js';
import { STARTUP_CONFIG, comment } from '../lib/config.js';

export default {
	path: ['show', STARTUP_CONFIG],
	roles: ['user', 'llm'],
	// what the page starts with over kiss.conf, as the lines that set it
	run(ctx) {
		const lines = Object.entries(ctx.archive.startup())
			.filter(([k]) => ctx.schema.known(k))
			.sort(([a], [b]) => a.localeCompare(b))
			.map(([k, v]) => ctx.schema.line(k, v));
		return lines.length ? lines.join('\n') : comment('the page starts on kiss.conf alone');
	}
} satisfies Command;
