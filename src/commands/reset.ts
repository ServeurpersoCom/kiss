import type { Command } from '../lib/types.js';

export default {
	path: ['reset'],
	roles: ['user', 'llm'],
	// the session sets nothing anymore: kiss.conf over the defaults
	run(ctx) {
		ctx.config.load({});
		return '';
	}
} satisfies Command;
