import type { Command } from '../lib/types.js';
import { comment } from '../lib/config.js';

export default {
	path: ['show', 'title'],
	roles: ['user', 'llm'],
	// the title of the conversation the batch was sent in, as the line that sets it
	run(ctx) {
		if (!ctx.conversation) throw new Error('no conversation here');
		const { title } = ctx.conversation;
		return title ? `title ${ctx.schema.quote(title)}` : comment('no title yet');
	}
} satisfies Command;
