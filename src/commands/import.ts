import type { Command } from '../lib/types.js';
import { ID_SHOWN, comment } from '../lib/config.js';

export default {
	path: ['import'],
	roles: ['user', 'llm'],
	alone: true,
	// the conversations of a file the user picks, added whole beside the others,
	// those already here skipped; no conversation changes
	async run(ctx) {
		if (!ctx.pick || !ctx.conversations) throw new Error('nobody is here to pick a file');
		const text = await ctx.pick();
		ctx.signal?.throwIfAborted();
		if (text === null) throw new Error('the user picked no file');
		const { added, skipped } = await ctx.conversations.unpack(text);
		const line = (id: string, title: string) =>
			`${id.slice(0, ID_SHOWN)} ${ctx.schema.quote(title)}`;
		return [
			...added.map((c) => `+ ${line(c.id, c.title)}`),
			...skipped.map((c) => comment(`${line(c.id, c.title)} is already here`))
		].join('\n');
	}
} satisfies Command;
