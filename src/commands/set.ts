import type { Command, Context, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { comment } from '../lib/config.js';

// no value for a secret: it is asked of the user
interface Plan {
	key: string;
	name?: string;
	value?: Value;
}

// the value of a secret the line leaves out, asked of the user, so that it
// never enters the conversation
async function asked(ctx: Context, key: string, name?: string): Promise<Value> {
	const head = name ? `${key} ${name}` : key;
	if (!ctx.secret) throw new Error(`nobody is here to give ${head}`);
	const typed = await ctx.secret(head);
	ctx.signal?.throwIfAborted();
	if (typed === null) throw new Error(`no value given for ${head}`);
	return ctx.schema.parse(key, typed);
}

export default {
	path: ['set'],
	roles: ['user', 'llm'],
	module: true,
	parse(schema, args) {
		const { key, def, name, rest } = schema.read(args);
		if (rest.length) return { key, name, value: schema.parse(key, rest.join(' ')) };
		if (def.kind !== 'secret') throw new Incomplete();
		return { key, name };
	},
	// an item it names is one its module may hold; a secret the user types says
	// so, so whoever wrote the line knows it is given
	async run(ctx, { key, name, value }) {
		if (name !== undefined) await ctx.admit(key.slice(0, key.indexOf(' ')), name);
		if (value !== undefined) {
			ctx.config.set(key, value, name);
			return '';
		}
		ctx.config.set(key, await asked(ctx, key, name), name);
		return comment(`the user typed ${name ? `${key} ${name}` : key}`);
	},
	complete(ctx, args) {
		return ctx.schema.next(ctx.config, args, true, false);
	}
} satisfies Command<Plan>;
