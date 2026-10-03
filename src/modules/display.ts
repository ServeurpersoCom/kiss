import type { Module } from '../lib/types.js';
import { display } from '../lib/display.svelte.js';

const FOLDS = ['closed', 'open'];

// how the thread shows a turn: whether its thinking and its tool calls open
// unfolded
export default {
	name: 'display',
	keys: {
		thinking: { kind: 'enum', values: FOLDS, default: 'closed' },
		tools: { kind: 'enum', values: FOLDS, default: 'closed' }
	},
	apply(config) {
		display.thinking = config.get('display thinking') === 'open';
		display.tools = config.get('display tools') === 'open';
	}
} satisfies Module;
