import type { Tool } from '../lib/types.js';
import prompts from '../lib/prompts.json';
import { ERROR_PREFIX } from '../lib/config.js';

// the lines a call holds, none when they are no string
const text = (args: Record<string, unknown>): string =>
	typeof args.lines === 'string' ? args.lines : '';

export default {
	name: 'config',
	description: prompts.config,
	parameters: {
		type: 'object',
		properties: {
			lines: { type: 'string', description: 'one or more CLI lines, one command per line' }
		},
		required: ['lines']
	},
	stored(ctx, args) {
		return { ...args, lines: ctx.redact(text(args)) };
	},
	async run(ctx, args) {
		const lines = text(args);
		if (!lines.trim()) return { ok: false, text: ERROR_PREFIX + 'no lines to run' };
		return ctx.cli(lines);
	}
} satisfies Tool;
