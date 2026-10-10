import type { Command, Context, Conversation } from '../lib/types.js';
import {
	ALL,
	ID_SHOWN,
	RUNNING_CONFIG,
	SITE_CONFIG_URL,
	beyond,
	comment,
	configuration,
	fileName,
	localTime
} from '../lib/config.js';
import { running } from './show-running.js';

// what export names: every conversation, one by a prefix of its id, the one
// the batch was sent in, or the running configuration
type Plan = typeof ALL | typeof RUNNING_CONFIG | { prefix: string } | null;

// the conversations a plan names
function chosen(ctx: Context, plan: Exclude<Plan, typeof RUNNING_CONFIG>): readonly Conversation[] {
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
		if (args[0] === ALL) return ALL;
		if (configuration(args[0]) === RUNNING_CONFIG) return RUNNING_CONFIG;
		return { prefix: args[0].toLowerCase() };
	},
	// a file offered to the user: the running configuration as kiss.conf, as a
	// site serves it and import reads it, secrets left out; one conversation
	// named after its title, or many named after the day; nothing changes
	async run(ctx, plan) {
		if (!ctx.offer) throw new Error('nobody is here to save the file');
		if (plan === RUNNING_CONFIG) {
			const saved = await ctx.offer(SITE_CONFIG_URL, running(ctx).join('\n') + '\n');
			ctx.signal?.throwIfAborted();
			if (!saved) throw new Error('the user saved no file');
			return comment(`exported the running configuration to ${SITE_CONFIG_URL}`);
		}
		const list = chosen(ctx, plan);
		if (!list.length) throw new Error('no conversation yet');
		const name =
			list.length === 1
				? fileName(list[0].title || list[0].id.slice(0, ID_SHOWN))
				: fileName(`kiss ${localTime(Date.now()).slice(0, 10)}`);
		const saved = await ctx.offer(name, ctx.conversations!.pack(list));
		ctx.signal?.throwIfAborted();
		if (!saved) throw new Error('the user saved no file');
		const n = list.length;
		return comment(`exported ${n} ${n === 1 ? 'conversation' : 'conversations'} to ${name}`);
	},
	complete(_ctx, args) {
		return args.length ? [] : [ALL, RUNNING_CONFIG, '<id>'];
	}
} satisfies Command<Plan>;
