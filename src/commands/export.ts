import type { Command, Context, Conversation } from '../lib/types.js';
import { ALL, ID_SHOWN, beyond, fileName, localTime } from '../lib/config.js';

// what export names: every conversation, one by a prefix of its id, or the one
// the batch was sent in
type Plan = typeof ALL | { prefix: string } | null;

// the conversations a plan names
function chosen(ctx: Context, plan: Plan): readonly Conversation[] {
	const all = ctx.conversations?.list() ?? [];
	if (plan === ALL) return all;
	if (plan === null) {
		const here = all.find((c) => c.id === ctx.conversation?.id);
		if (!here) throw new Error('no conversation here');
		return [here];
	}
	return [ctx.conversations!.find(plan.prefix)];
}

export default {
	path: ['export'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (args.length > 1) throw beyond(['export', args[0]], args.slice(1));
		if (!args.length) return null;
		return args[0] === ALL ? ALL : { prefix: args[0].toLowerCase() };
	},
	// a file offered to the user, of one conversation named after its title, or
	// of many named after the day; no conversation changes
	async run(ctx, plan) {
		const list = chosen(ctx, plan);
		if (!list.length) throw new Error('no conversation yet');
		if (!ctx.offer) throw new Error('nobody is here to save the file');
		const name =
			list.length === 1
				? fileName(list[0].title || list[0].id.slice(0, ID_SHOWN))
				: fileName(`kiss ${localTime(Date.now()).slice(0, 10)}`);
		const saved = await ctx.offer(name, ctx.conversations!.pack(list));
		ctx.signal?.throwIfAborted();
		if (!saved) throw new Error('the user saved no file');
		return `exported ${list.length} to ${name}`;
	},
	complete(_ctx, args) {
		return args.length ? [] : [ALL, '<id>'];
	}
} satisfies Command<Plan>;
