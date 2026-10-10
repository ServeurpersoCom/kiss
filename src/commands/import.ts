import type { Command } from '../lib/types.js';
import { ID_SHOWN, comment, titled } from '../lib/config.js';
import { splitLines } from '../engine/parse.js';

// whether a text reads as JSON
function json(text: string): boolean {
	try {
		JSON.parse(text);
		return true;
	} catch {
		return false;
	}
}

export default {
	path: ['import'],
	roles: ['user', 'llm'],
	alone: true,
	// a file the user picks, read by what it holds and told so: the
	// conversations of a JSON file added whole beside the others, those already
	// here skipped, or the lines of a configuration run as more lines of the
	// batch, with the rights of whoever asked; a file that holds nothing fails
	async run(ctx) {
		if (!ctx.pick || !ctx.conversations) throw new Error('nobody is here to pick a file');
		const text = await ctx.pick();
		ctx.signal?.throwIfAborted();
		if (text === null) throw new Error('the user picked no file');
		if (!json(text)) {
			const n = splitLines(text).length;
			if (!n) throw new Error('the file holds no line');
			const out = await ctx.lines!(text);
			return [comment(`ran ${n} ${n === 1 ? 'line' : 'lines'} of the file`), out]
				.filter((l) => l)
				.join('\n');
		}
		const { added, skipped } = await ctx.conversations.unpack(text);
		if (!added.length && !skipped.length) throw new Error('the file holds no conversation');
		const line = (id: string, title: string) =>
			id.slice(0, ID_SHOWN) + titled(title, (v) => ctx.schema.quote(v));
		return [
			...added.map((c) => `+ ${line(c.id, c.title)}`),
			...skipped.map((c) => comment(`${line(c.id, c.title)} is already here`))
		].join('\n');
	}
} satisfies Command;
