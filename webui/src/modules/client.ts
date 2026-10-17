import type { Module } from '../engine/engine.js';
import { ATTACHED, BACKEND, OFF, ON, follow } from '../lib/sync.js';

// what the page is to a KiSS server: attached, a view of the conversations of
// the server mcp backend names, its agent answering them and every CLI line
// running there; detached, the page holds its own conversations, answers them
// itself and runs every line; the two never mix
export default {
	name: 'client',
	keys: {
		attached: { kind: 'enum', values: ATTACHED, default: OFF }
	},
	validate(config) {
		return config.get('client attached') === ON && !config.get('mcp url', BACKEND)
			? `client attached needs mcp url ${BACKEND}, the KiSS server the page attaches to`
			: null;
	},
	apply(config) {
		follow(config);
	}
} satisfies Module;
