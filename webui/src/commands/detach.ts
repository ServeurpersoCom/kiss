import type { Command } from '../engine/engine.js';
import { attach } from '../lib/sync.js';

// the page detaches from its KiSS server
export default {
	path: ['detach'],
	roles: ['user'],
	run(ctx) {
		attach(ctx.running, false);
		return '';
	}
} satisfies Command;
