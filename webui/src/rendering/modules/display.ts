import type { Module } from '../../engine/engine.js';
import {
	BLOCKS,
	CLOSED,
	FOLDS,
	MARKDOWN,
	RENDERS,
	display,
	type Fold,
	type Render
} from '../display.svelte.js';

// how the thread shows a turn: how its thinking and its tool calls fold, how
// its thinking and its reply render, and the argument a folded call shows by
// its name, description, the why every tool of KiSS and of its server takes
export default {
	name: 'display',
	keys: {
		thinking: { kind: 'enum', values: FOLDS, default: CLOSED },
		'tool-calls': { kind: 'enum', values: FOLDS, default: CLOSED },
		render: { kind: 'enum', values: RENDERS, default: MARKDOWN, named: true, names: () => BLOCKS },
		'preview-argument': { kind: 'string', default: 'description' }
	},
	apply(config) {
		display.thinking = config.get('display thinking') as Fold;
		display.tools = config.get('display tool-calls') as Fold;
		for (const b of BLOCKS) display.render[b] = config.get('display render', b) as Render;
		display.argument = String(config.get('display preview-argument') ?? '');
	},
	// the blocks of text a turn renders
	async items() {
		return [{ group: '', names: [...BLOCKS] }];
	}
} satisfies Module;
