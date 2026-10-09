import type { Module } from '../lib/types.js';
import { CLOSED, FOLDS, display, type Fold } from '../lib/display.svelte.js';

// how the thread shows a turn: how its thinking and its tool calls fold
export default {
	name: 'display',
	keys: {
		thinking: { kind: 'enum', values: FOLDS, default: CLOSED },
		tools: { kind: 'enum', values: FOLDS, default: CLOSED }
	},
	apply(config) {
		display.thinking = config.get('display thinking') as Fold;
		display.tools = config.get('display tools') as Fold;
	}
} satisfies Module;
