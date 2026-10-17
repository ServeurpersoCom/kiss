import type { Command } from '../engine/engine.js';
import { OFF } from '../lib/sync.js';

// the page detaches from the KiSS server: client attached off
export default {
	path: ['detach'],
	roles: ['user'],
	run(ctx) {
		ctx.config.set('client attached', OFF);
		return '';
	}
} satisfies Command;
