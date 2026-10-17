import type { Command } from '../engine/engine.js';
import { STARTUP_CONFIG, comment } from '../engine/text.js';
import { listed } from './copy.js';

export default {
	path: ['show', STARTUP_CONFIG],
	roles: ['user', 'llm'],
	// what the page starts with, the site file included
	run(ctx) {
		const lines = listed(ctx, STARTUP_CONFIG);
		return lines.length ? lines.join('\n') : comment('every key is at its default');
	}
} satisfies Command;
