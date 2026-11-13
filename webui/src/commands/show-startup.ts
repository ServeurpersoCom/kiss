import type { Command } from '../engine/engine.js';
import { STARTUP_CONFIG, comment } from '../engine/text.js';
import { listed } from './copy.js';
import { NEWLINE } from '../lib/config.js';

export default {
	path: ['show', STARTUP_CONFIG],
	roles: ['user', 'llm'],
	// what the page starts with, client.conf included
	run(ctx) {
		const lines = listed(ctx, STARTUP_CONFIG);
		return lines.length ? lines.join(NEWLINE) : comment('every key is at its default');
	}
} satisfies Command;
