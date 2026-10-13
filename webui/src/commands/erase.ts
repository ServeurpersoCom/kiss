import type { Command, Context } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import {
	CONVERSATIONS,
	ID_SHOWN,
	RUNNING_CONFIG,
	STARTUP_CONFIG,
	beyond,
	comment,
	titled
} from '../lib/config.js';
import { configs, named, target } from './copy.js';

// what erase names, a prefix being enough: every conversation but the pinned
// ones, a configuration, or a conversation by its id
type Plan = string;

// a configuration erased: the running-config back to client.conf over the
// defaults, the startup-config gone so the page starts on client.conf, a save
// gone
function config(ctx: Context, name: string): string {
	if (name === RUNNING_CONFIG) {
		ctx.config.load({});
		return '';
	}
	if (name === STARTUP_CONFIG) {
		ctx.archive.boot(undefined);
		return comment(`erased ${STARTUP_CONFIG}, the page starts on client.conf`);
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
		return args[0];
	},
	// what it erases is figured once, so the yes of the user erases what the
	// question named and nothing that came since
	async run(ctx, word) {
		if (!ctx.confirm) throw new Error('nobody is here to confirm the erase');
		const plan = ctx.once(() => target(ctx, word));
		if (named(ctx, plan)) {
			const yes = await ctx.confirm(`Erase ${plan}?`);
			ctx.signal?.throwIfAborted();
			if (!yes) throw new Error('the user erased nothing');
			return config(ctx, plan);
		}
		const library = ctx.conversations;
		if (!library) throw new Error('no conversation to erase');
		const list = ctx.once(() =>
			(plan === CONVERSATIONS ? library.list().filter((c) => !c.pinned) : [library.find(plan)]).map(
				({ id, title }) => ({ id, title })
			)
		);
		if (!list.length) throw new Error('no conversation to erase');
		const question =
			list.length === 1
				? `Erase${titled(list[0].title, (v) => ctx.schema.quote(v)) || ' this conversation'}?`
				: `Erase ${list.length} conversations?`;
		const yes = await ctx.confirm(question);
		ctx.signal?.throwIfAborted();
		if (!yes) throw new Error('the user erased nothing');
		await library.remove(list.map((c) => c.id));
		return list
			.map((c) => `- ${c.id.slice(0, ID_SHOWN)}${titled(c.title, (v) => ctx.schema.quote(v))}`)
			.join('\n');
	},
	complete(ctx, args) {
		return args.length ? [] : [CONVERSATIONS, ...configs(ctx), '<id>'];
	}
} satisfies Command<Plan>;
