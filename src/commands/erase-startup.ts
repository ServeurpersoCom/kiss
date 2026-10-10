import type { Command } from '../lib/types.js';
import { STARTUP_CONFIG, comment } from '../lib/config.js';

export default {
	path: ['erase', STARTUP_CONFIG],
	roles: ['user'],
	alone: true,
	// the page starts on kiss.conf alone
	run(ctx) {
		ctx.archive.boot(undefined);
		return comment(`erased ${STARTUP_CONFIG}, the page starts on kiss.conf`);
	}
} satisfies Command;
