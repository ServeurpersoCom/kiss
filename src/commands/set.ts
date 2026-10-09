import type { Command, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';

interface Plan {
	key: string;
	name?: string;
	value: Value;
}

export default {
	path: ['set'],
	roles: ['user', 'llm'],
	module: true,
	parse(schema, args) {
		const { key, name, rest } = schema.read(args);
		if (!rest.length) throw new Incomplete();
		return { key, name, value: schema.parse(key, rest.join(' ')) };
	},
	run(ctx, { key, name, value }) {
		ctx.config.set(key, value, name);
		return '';
	},
	complete(ctx, args) {
		return ctx.schema.next(ctx.config, args, true, false);
	}
} satisfies Command<Plan>;
