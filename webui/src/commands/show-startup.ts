import type { Command } from '../lib/types.js';
import { STARTUP_CONFIG, comment } from '../lib/config.js';
import { listed } from './copy.js';

export default {
	path: ['show', STARTUP_CONFIG],
	roles: ['user', 'llm'],
	// what the page starts with, kiss.conf included
	run(ctx) {
		const lines = listed(ctx, STARTUP_CONFIG);
		return lines.length ? lines.join('\n') : comment('every key is at its default');
	}
} satisfies Command;
