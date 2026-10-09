import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { TITLE_LENGTH } from '../lib/config.js';

export default {
	path: ['title'],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (!args.length) throw new Incomplete();
		const title = args.join(' ');
		if (!title.trim()) throw new Error('a title holds a word at least');
		if (title.includes('\n')) throw new Error('a title holds one line');
		if (title.length > TITLE_LENGTH) {
			throw new Error(`a title holds ${TITLE_LENGTH} characters at most`);
		}
		return title;
	},
	// the conversation the batch was sent in takes it with the batch; a title
	// is no configuration, no save keeps it
	run(ctx, title) {
		if (!ctx.conversation) throw new Error('no conversation here');
		ctx.conversation.title = title;
		return '';
	},
	complete(_ctx, args) {
		return args.length ? [] : ['<title>'];
	}
} satisfies Command<string>;
