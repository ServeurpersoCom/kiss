import type { Command } from '../engine/engine.js';
import { choose } from '../engine/near.js';

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
			if (schema.collection(module) && !schema.keyAfter(module, args[1], false)) {
				return { module, item: args[1] };
			}
		}
		const { key, name } = schema.read(args);
		return { key, name };
	},
	// an item names one the draft holds, a prefix being enough
	run(ctx, plan) {
		if ('item' in plan) {
			ctx.config.drop(plan.module, choose(plan.item, ctx.config.names(plan.module), plan.module));
		} else {
			const [module] = plan.key.split(' ');
			const name = plan.name && choose(plan.name, ctx.config.names(module), module);
			ctx.config.unset(plan.key, name);
		}
		return '';
	},
	complete(ctx, args) {
		return ctx.schema.next(ctx.config, args, false, true);
	}
} satisfies Command<Plan>;
