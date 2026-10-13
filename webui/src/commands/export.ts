import type { Command, Context, Conversation } from '../lib/types.js';
import {
	CONVERSATION,
	CONVERSATIONS,
	CONFIG_EXTENSION,
	FILE_EXTENSION,
	ID_SHOWN,
	beyond,
	comment,
	fileName,
	localTime
} from '../lib/config.js';
import { configs, listed, named, target } from './copy.js';

// what export names: the conversation the batch was sent in, or a word, a
// prefix being enough: every conversation, a configuration, or a conversation
// by its id
type Plan = string | null;

// a file offered: its name, its text, and what the output tells of it
interface File {
	name: string;
	text: string;
	told: string;
}

// a configuration with its secrets as it runs, client.conf included, as import
// reads it and a site serves it
function configFile(ctx: Context, config: string, minute: string): File {
	const lines = listed(ctx, config, true);
	const name = fileName(`${config} ${minute}`, CONFIG_EXTENSION);
	return { name, text: lines.join('\n') + '\n', told: `exported ${config} to ${name}` };
}

// the conversations, named after the minute, or one, named after its title
function packed(ctx: Context, plan: Plan, minute: string): File {
	const all = ctx.conversations?.list() ?? [];
	let list: readonly Conversation[] = all;
	if (plan === null) {
		const here = all.find((c) => c.id === ctx.conversation?.id);
		if (!here) throw new Error('no conversation here');
		list = [here];
	} else if (plan !== CONVERSATIONS) list = [ctx.conversations!.find(plan)];
	if (!list.length) throw new Error('no conversation yet');
	const title =
		plan === CONVERSATIONS
			? `${CONVERSATIONS} ${minute}`
			: `${CONVERSATION} ${list[0].title || list[0].id.slice(0, ID_SHOWN)}`;
	const name = fileName(title, FILE_EXTENSION);
	const n = list.length;
	const told = `exported ${n} ${n === 1 ? 'conversation' : 'conversations'} to ${name}`;
	return { name, text: ctx.conversations!.pack(list), told };
}

export default {
	path: ['export'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (args.length > 1) throw beyond(['export', args[0]], args.slice(1));
		return args.length ? args[0] : null;
	},
	// a file offered to the user, its content going to their disk alone, named
	// after what it holds: a configuration with its secrets, the running-config,
	// the startup-config or a save, then the minute, the conversations then the
	// minute, one conversation then its title, figured once so the file saved
	// is the one offered; nothing changes
	async run(ctx, word) {
		if (!ctx.offer) throw new Error('nobody is here to save the file');
		const file = ctx.once(() => {
			const minute = localTime(Date.now());
			const plan = word === null ? null : target(ctx, word);
			return plan !== null && named(ctx, plan)
				? configFile(ctx, plan, minute)
				: packed(ctx, plan, minute);
		});
		const saved = await ctx.offer(file.name, file.text);
		ctx.signal?.throwIfAborted();
		if (!saved) throw new Error('the user saved no file');
		return comment(file.told);
	},
	complete(ctx, args) {
		return args.length ? [] : [CONVERSATIONS, ...configs(ctx), '<id>'];
	}
} satisfies Command<Plan>;
