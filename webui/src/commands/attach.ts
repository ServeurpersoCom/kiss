import type { Command } from '../engine/engine.js';
import { ON } from '../lib/sync.js';

// the page attaches to the KiSS server: client attached on
export default {
	path: ['attach'],
	roles: ['user'],
	run(ctx) {
		ctx.config.set('client attached', ON);
		return '';
	}
} satisfies Command;
