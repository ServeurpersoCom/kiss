import type { Module } from '../../engine/engine.js';
import { display } from '../display.svelte.js';

// how the thread shows the calls of a tool, the tool named as the item, any
// tool of KiSS or of an MCP server: the argument a call shows folded, over
// the one display preview-argument names for every tool
export default {
	name: 'tools',
	keys: {
		'preview-argument': { kind: 'string', named: true }
	},
	apply(config) {
		display.preview = Object.fromEntries(
			config
				.names('tools')
				.map((tool) => [tool, String(config.get('tools preview-argument', tool) ?? '')])
				.filter(([, argument]) => argument)
		);
	}
} satisfies Module;
