import type { Command, Context, Conversation } from '../lib/types.js';
import {
	ALL,
	CONFIG_EXTENSION,
	FILE_EXTENSION,
	ID_SHOWN,
	RUNNING_CONFIG,
	STARTUP_CONFIG,
	beyond,
	comment,
	configuration,
	fileName,
	localTime
} from '../lib/config.js';
import { listed, named } from './copy.js';

// what export names: every conversation, the one the batch was sent in, or a
// word: a configuration, else a conversation by a prefix of its id
type Plan = typeof ALL | null | { word: string };

// a file offered: its name, its text, and what the output tells of it
interface File {
	name: string;
	text: string;
	told: string;
}

// a configuration with its secrets as it runs, kiss.conf included, as import
// reads it and a site serves it
function configFile(ctx: Context, config: string, minute: string): File {
	const lines = listed(ctx, config, true);
	const name = fileName(`${config} ${minute}`, CONFIG_EXTENSION);
	return { name, text: lines.join('\n') + '\n', told: `exported ${config} to ${name}` };
}

// conversations: all of them named after the minute, one after its title
function packed(ctx: Context, plan: Plan, minute: string): File {
	const all = ctx.conversations?.list() ?? [];
	let list: readonly Conversation[] = all;
	if (plan === null) {
		const here = all.find((c) => c.id === ctx.conversation?.id);
		if (!here) throw new Error('no conversation here');
		list = [here];
	} else if (plan !== ALL) list = [ctx.conversations!.find(plan.word.toLowerCase())];
	if (!list.length) throw new Error('no conversation yet');
	const title = plan === ALL ? `${ALL} ${minute}` : list[0].title || list[0].id.slice(0, ID_SHOWN);
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
		if (!args.length) return null;
		if (args[0] === ALL) return ALL;
		return { word: configuration(args[0]) };
	},
	// a file offered to the user, its content going to their disk alone, named
	// after what it holds: a configuration with its secrets, the running-config,
	// the startup-config or a save, and all the conversations, after the minute
	// too; one conversation after its title; nothing changes
	async run(ctx, plan) {
		if (!ctx.offer) throw new Error('nobody is here to save the file');
		const minute = localTime(Date.now());
		const config = plan !== null && plan !== ALL ? plan.word : '';
		const file = named(ctx, config) ? configFile(ctx, config, minute) : packed(ctx, plan, minute);
		const saved = await ctx.offer(file.name, file.text);
		ctx.signal?.throwIfAborted();
		if (!saved) throw new Error('the user saved no file');
		return comment(file.told);
	},
	complete(ctx, args) {
		const configs = [RUNNING_CONFIG, STARTUP_CONFIG, ...ctx.archive.list().map((s) => s.name)];
		return args.length ? [] : [ALL, ...configs, '<id>'];
	}
} satisfies Command<Plan>;
