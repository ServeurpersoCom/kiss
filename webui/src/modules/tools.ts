import type { Module } from '../lib/types.js';
import { NAME } from '../lib/config.js';
import { display } from '../lib/display.svelte.js';
import { served } from '../lib/mcp.js';
import { CONSENT, USES, tools } from '../lib/tools.js';

// every setting of a tool, the tool named as the item: one of KiSS or of an
// MCP server
export default {
	name: 'tools',
	keys: {
		// the rounds of tool calls a turn takes at most
		'max-rounds': { kind: 'number', min: 1, integer: true, default: '1000' },
		// off, the model never sees the tool; consent, each call asks the user,
		// always turning it on; the factory configuration turns the tools of
		// KiSS on, and their changes ask
		use: { kind: 'enum', values: USES, default: CONSENT, named: true, guard: true },
		// the argument a call of the tool shows folded, by its name, over the
		// one display preview-argument names for every tool
		'preview-argument': { kind: 'string', named: true }
	},
	apply(config) {
		display.preview = Object.fromEntries(
			config
				.names('tools')
				.map((tool) => [tool, String(config.get('tools preview-argument', tool) ?? '')])
				.filter(([, argument]) => argument)
		);
	},
	// the tools of KiSS, then those of every MCP server
	async items(ctx) {
		const mcp = await served(ctx.running, new Map());
		return [
			{ group: NAME, names: tools.map((t) => t.name) },
			...mcp.map((s) => ({
				group: `mcp ${s.server}`,
				names: (s.tools ?? []).map((t) => t.name),
				...(s.error ? { error: s.error } : {})
			}))
		];
	}
} satisfies Module;
