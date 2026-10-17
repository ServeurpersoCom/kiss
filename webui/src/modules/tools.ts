import type { Module } from '../engine/engine.js';
import { NAME } from '../lib/config.js';
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
		use: { kind: 'enum', values: USES, default: CONSENT, named: true, guard: true }
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
