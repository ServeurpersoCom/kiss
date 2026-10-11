import type { Module } from '../lib/types.js';
import {
	BLOCKS,
	CLOSED,
	FOLDS,
	MARKDOWN,
	RENDERS,
	display,
	type Fold,
	type Render
} from '../lib/display.svelte.js';

// how the thread shows a turn: how its thinking and its tool calls fold, and
// how its thinking and its reply render
export default {
	name: 'display',
	keys: {
		thinking: { kind: 'enum', values: FOLDS, default: CLOSED },
		tools: { kind: 'enum', values: FOLDS, default: CLOSED },
		render: { kind: 'enum', values: RENDERS, default: MARKDOWN, named: true, names: () => BLOCKS }
	},
	apply(config) {
		display.thinking = config.get('display thinking') as Fold;
		display.tools = config.get('display tools') as Fold;
		for (const b of BLOCKS) display.render[b] = config.get('display render', b) as Render;
	},
	// the blocks of text a turn renders
	async items() {
		return [{ group: '', names: [...BLOCKS] }];
	}
} satisfies Module;
