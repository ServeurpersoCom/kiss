import type { Tool } from '../lib/types.js';
import prompts from '../lib/prompts.json';
import { ERROR_PREFIX } from '../lib/config.js';

// the lines a call holds, none when they are no string
const text = (args: Record<string, unknown>): string =>
	typeof args.lines === 'string' ? args.lines : '';

export default {
	name: 'client_config',
	description: prompts.client_config,
	parameters: {
		type: 'object',
		properties: {
			description: { type: 'string', description: "Why I'm configuring the page, written first" },
			lines: { type: 'string', description: 'one or more CLI lines, one command per line' }
		},
		required: ['description', 'lines']
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
