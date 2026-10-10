import type { Command } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { ALL, ID_SHOWN, beyond, titled } from '../lib/config.js';

// what delete names: every conversation but the pinned ones, or one by a prefix
// of its id
type Plan = typeof ALL | { prefix: string };

export default {
	path: ['delete'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (!args.length) throw new Incomplete();
		if (args.length > 1) throw beyond(['delete', args[0]], args.slice(1));
		return args[0] === ALL ? ALL : { prefix: args[0].toLowerCase() };
	},
	// the conversations named, gone at once once the user confirms it, whoever
	// asks; a pinned conversation goes only when named
	async run(ctx, plan) {
		if (!ctx.conversations || !ctx.confirm) throw new Error('nobody is here to confirm the delete');
		const list =
			plan === ALL
				? ctx.conversations.list().filter((c) => !c.pinned)
				: [ctx.conversations.find(plan.prefix)];
		if (!list.length) throw new Error('no conversation to delete');
		const question =
			list.length === 1
				? `Delete${titled(list[0].title, (v) => ctx.schema.quote(v)) || ' this conversation'}?`
				: `Delete ${list.length} conversations?`;
		const yes = await ctx.confirm(question);
		ctx.signal?.throwIfAborted();
		if (!yes) throw new Error('the user deleted nothing');
		await ctx.conversations.remove(list.map((c) => c.id));
		return list
			.map((c) => `- ${c.id.slice(0, ID_SHOWN)}${titled(c.title, (v) => ctx.schema.quote(v))}`)
			.join('\n');
	},
	complete(_ctx, args) {
		return args.length ? [] : [ALL, '<id>'];
	}
} satisfies Command<Plan>;
