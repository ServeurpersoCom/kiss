import type { Command } from '../lib/types.js';

export default {
	path: ['show', 'title'],
	roles: ['user', 'llm'],
	// the title of the conversation the batch was sent in, as the line that sets it
	run(ctx) {
		if (!ctx.conversation) throw new Error('no conversation here');
		return `title ${ctx.schema.quote(ctx.conversation.title)}`;
	}
} satisfies Command;
