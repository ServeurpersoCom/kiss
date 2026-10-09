import type { Command, Context, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { SESSION, beyond, comment } from '../lib/config.js';

// the values of a save, or of the running configuration for session
function values(ctx: Context, name: string): Record<string, Value> {
	return name === SESSION ? ctx.config.values() : ctx.archive.find(name).values;
}

export default {
	path: ['show', 'diff'],
	roles: ['user', 'llm'],
	parse(_schema, args) {
		if (args.length < 2) throw new Incomplete();
		if (args.length > 2) throw beyond(['show', 'diff', ...args.slice(0, 2)], args.slice(2));
		return args;
	},
	// from one save to another, session naming the running configuration
	run(ctx, [from, to]) {
		const lines = ctx.schema.diff(values(ctx, from), values(ctx, to));
		return lines.length ? lines.join('\n') : comment('no difference');
	},
	complete(ctx, args) {
		return args.length < 2 ? [...ctx.archive.list().map((s) => s.name), SESSION] : [];
	}
} satisfies Command<string[]>;
