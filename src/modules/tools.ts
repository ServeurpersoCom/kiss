import type { Module } from '../lib/types.js';
import { NAME } from '../lib/config.js';
import { display } from '../lib/display.svelte.js';
import { served } from '../lib/mcp.js';
import { tools } from '../lib/tools.js';

// every setting of a tool, the tool named as the item: one of KiSS or of an
// MCP server
export default {
	name: 'tools',
	keys: {
		// the rounds of tool calls a turn takes at most
		rounds: { kind: 'number', min: 1, integer: true, default: '25' },
		// off, the model never sees the tool
		use: { kind: 'enum', values: ['on', 'off'], default: 'on', named: true },
		// the argument a call of the tool shows folded; unset, its first one
		preview: { kind: 'string', named: true }
	},
	apply(config) {
		display.preview = Object.fromEntries(
			config
				.names('tools')
				.map((tool) => [tool, String(config.get('tools preview', tool) ?? '')])
				.filter(([, argument]) => argument)
		);
	},
	// the tools of KiSS, then those of every MCP server
	async items(config) {
		const mcp = await served(config);
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
