import type { Command } from '../engine/engine.js';
import { attach } from '../lib/sync.js';

// the page detaches from its KiSS server, on a line of its own as the mode
// of the page changes at once, outside any batch
export default {
	path: ['detach'],
	roles: ['user'],
	alone: true,
	run(ctx) {
		attach(ctx.running, false);
		return '';
	}
} satisfies Command;
