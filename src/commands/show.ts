import type { Command, Context, Group } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { comment } from '../lib/config.js';

// show chat, show tools use, show tools use bash_tool, and show tools bash_tool:
// the word after a collection names a key when it names one, else an item
interface Plan {
	module: string;
	key?: string;
	name?: string;
}

// the lines of the keys the plan names for one item, or for the keys of no
// item; a value not set shows only when the plan names it exactly
function lines(ctx: Context, plan: Plan, name: string | undefined): string[] {
	const out: string[] = [];
	for (const [key, def] of ctx.schema.list()) {
		if (!key.startsWith(plan.module + ' ') || (plan.key && key !== plan.key)) continue;
		if (!!def.named !== (name !== undefined)) continue;
		const head = name ? `${key} ${name}` : key;
		const value = ctx.config.get(key, name);
		if (value !== undefined) out.push(ctx.schema.line(head, value));
		else if (plan.key && (plan.name || !def.named)) out.push(comment(`${head} is not set`));
	}
	return out;
}

// the lines of groups, each body under its header: a failed group holds its
// error on the header, an empty one shows nothing
function grouped(groups: Group[], body: (name: string) => string[]): string[] {
	return groups.flatMap((g) => {
		if (g.error) return [comment(`${g.group} ${g.error}`)];
		const lines = g.names.flatMap(body);
		return lines.length ? [comment(g.group), ...lines] : [];
	});
}

export default {
	path: ['show'],
	roles: ['user', 'llm'],
	parse(schema, args) {
		if (!args.length) throw new Incomplete();
		const module = schema.module(args[0]);
		if (args.length === 1) return { module };
		const word = schema.key(module, args[1]);
		if (!word) {
			if (!schema.collection(module)) throw new Error(`unknown key "${module} ${args[1]}"`);
			if (args.length > 2) throw new Error(`nothing goes after: ${args.slice(2).join(' ')}`);
			return { module, name: args[1] };
		}
		const key = `${module} ${word}`;
		const words = schema.find(key)!.named ? 3 : 2;
		if (args.length > words) throw new Error(`nothing goes after: ${args.slice(words).join(' ')}`);
		return { module, key, name: args[2] };
	},
	// the values the words name, defaults included, as set lines to paste back;
	// the items of a module that knows more than those set go by group, who
	// knows them, the others last
	async run(ctx, plan) {
		const out = plan.name ? [] : lines(ctx, plan, undefined);
		const set = ctx.config.names(plan.module);
		const items = ctx.modules.find((m) => m.name === plan.module)!.items;
		if (plan.name) out.push(...lines(ctx, plan, plan.name));
		else if (!items) out.push(...set.flatMap((n) => lines(ctx, plan, n)));
		else {
			const groups = await items(ctx.config, ctx.signal);
			const known = new Set(groups.flatMap((g) => g.names));
			groups.push({ group: 'others', names: set.filter((n) => !known.has(n)) });
			// an item without a setting shows by its name alone, unless a key is named
			const body = (n: string) => {
				const shown = lines(ctx, plan, n);
				return shown.length || plan.key ? shown : [comment(n)];
			};
			out.push(...grouped(groups, body));
		}
		return (
			out.join('\n') || comment(`no ${[plan.module, plan.name].filter((w) => w).join(' ')} yet`)
		);
	},
	complete(ctx, args) {
		return ctx.schema.next(ctx.config, args, false, true);
	}
} satisfies Command<Plan>;
