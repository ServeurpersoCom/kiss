import type { Command, Context } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import {
	ALL,
	ID_SHOWN,
	RUNNING_CONFIG,
	STARTUP_CONFIG,
	beyond,
	comment,
	configuration,
	titled
} from '../lib/config.js';
import { named } from './copy.js';

// what erase names: every conversation but the pinned ones, or a word: a
// configuration, else a conversation by a prefix of its id
type Plan = typeof ALL | { word: string };

// a configuration erased: the running-config back to kiss.conf over the
// defaults, the startup-config gone so the page starts on kiss.conf, a save
// gone
function config(ctx: Context, name: string): string {
	if (name === RUNNING_CONFIG) {
		ctx.config.load({});
		return '';
	}
	if (name === STARTUP_CONFIG) {
		ctx.archive.boot(undefined);
		return comment(`erased ${STARTUP_CONFIG}, the page starts on kiss.conf`);
	}
	ctx.archive.remove(ctx.archive.find(name));
	return comment(`erased ${name}`);
}

// IOS erase: a whole thing gone at once once the user confirms it, whoever
// asks: a configuration, or conversations, a pinned one only when named
export default {
	path: ['erase'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (!args.length) throw new Incomplete();
		if (args.length > 1) throw beyond(['erase', args[0]], args.slice(1));
		return args[0] === ALL ? ALL : { word: configuration(args[0]) };
	},
	async run(ctx, plan) {
		if (!ctx.confirm) throw new Error('nobody is here to confirm the erase');
		if (plan !== ALL && named(ctx, plan.word)) {
			const yes = await ctx.confirm(`Erase ${plan.word}?`);
			ctx.signal?.throwIfAborted();
			if (!yes) throw new Error('the user erased nothing');
			return config(ctx, plan.word);
		}
		if (!ctx.conversations) throw new Error('no conversation to erase');
		const list =
			plan === ALL
				? ctx.conversations.list().filter((c) => !c.pinned)
				: [ctx.conversations.find(plan.word.toLowerCase())];
		if (!list.length) throw new Error('no conversation to erase');
		const question =
			list.length === 1
				? `Erase${titled(list[0].title, (v) => ctx.schema.quote(v)) || ' this conversation'}?`
				: `Erase ${list.length} conversations?`;
		const yes = await ctx.confirm(question);
		ctx.signal?.throwIfAborted();
		if (!yes) throw new Error('the user erased nothing');
		await ctx.conversations.remove(list.map((c) => c.id));
		return list
			.map((c) => `- ${c.id.slice(0, ID_SHOWN)}${titled(c.title, (v) => ctx.schema.quote(v))}`)
			.join('\n');
	},
	complete(ctx, args) {
		const configs = [RUNNING_CONFIG, STARTUP_CONFIG, ...ctx.archive.list().map((s) => s.name)];
		return args.length ? [] : [ALL, ...configs, '<id>'];
	}
} satisfies Command<Plan>;
