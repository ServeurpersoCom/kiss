import type { Command, Context, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';

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
	async run(ctx, { key, name, value }) {
		ctx.config.set(key, value ?? (await asked(ctx, key, name)), name);
		return '';
	},
	complete(ctx, args) {
		return ctx.schema.next(ctx.config, args, true, false);
	}
} satisfies Command<Plan>;
