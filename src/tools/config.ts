import type { Tool } from '../lib/types.js';
import prompts from '../lib/prompts.json';
import { ERROR_PREFIX } from '../lib/config.js';

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
	async run(ctx, args) {
		const lines = typeof args.lines === 'string' ? args.lines : '';
		if (!lines.trim()) return { ok: false, text: ERROR_PREFIX + 'no lines to run' };
		const result = await ctx.cli(lines);
		return { ok: result.ok, text: result.text || 'ok', args: { lines: ctx.redact(lines) } };
	}
} satisfies Tool;
