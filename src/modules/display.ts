import type { Module } from '../lib/types.js';
import { display } from '../lib/display.svelte.js';

// folded, unfolded, or unfolded while the model writes them only
const FOLDS = ['closed', 'open', 'inference'];

// how the thread shows a turn: how its thinking and its tool calls fold
export default {
	name: 'display',
	keys: {
		thinking: { kind: 'enum', values: FOLDS, default: 'closed' },
		tools: { kind: 'enum', values: FOLDS, default: 'closed' }
	},
	apply(config) {
		display.thinking = String(config.get('display thinking'));
		display.tools = String(config.get('display tools'));
	}
} satisfies Module;
