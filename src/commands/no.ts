import type { Command } from '../lib/types.js';

// no endpoints prod names an item of a collection, no chat model a key; a
// key takes the line show running-config writes, whatever value follows it
type Plan = { module: string; item: string } | { key: string; name?: string };

export default {
	path: ['no'],
	roles: ['user', 'llm'],
	module: true,
	parse(schema, args) {
		if (args.length === 2) {
			const module = schema.module(args[0]);
			if (schema.collection(module) && !schema.key(module, args[1])) {
				return { module, item: args[1] };
			}
		}
		const { key, name } = schema.read(args);
		return { key, name };
	},
	run(ctx, plan) {
		if ('item' in plan) {
			if (!ctx.config.names(plan.module).includes(plan.item)) {
				throw new Error(`unknown ${plan.module} "${plan.item}"`);
			}
			ctx.config.drop(plan.module, plan.item);
		} else {
			ctx.config.unset(plan.key, plan.name);
		}
		return '';
	},
	complete(ctx, args) {
		return ctx.schema.next(ctx.config, args, false, true);
	}
} satisfies Command<Plan>;
