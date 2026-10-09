// how a block folds: folded, unfolded, or unfolded while the model writes it only
export const FOLDS = ['closed', 'open', 'inference'] as const;
export type Fold = (typeof FOLDS)[number];
export const [CLOSED, OPEN, INFERENCE] = FOLDS;

// how a block of text renders: as markdown, or plain in monospace as written
export const RENDERS = ['markdown', 'plain'] as const;
export type Render = (typeof RENDERS)[number];
export const [MARKDOWN, PLAIN] = RENDERS;

// the blocks of text a turn renders
export const BLOCKS = ['thinking', 'reply'] as const;
export type Block = (typeof BLOCKS)[number];

// how the thread shows a turn: how the thinking and the tool calls fold and
// how each block of text renders, from the display module, and the argument
// each tool shows folded, by tool name, from the tools module
export const display = $state({
	thinking: CLOSED as Fold,
	tools: CLOSED as Fold,
	render: Object.fromEntries(BLOCKS.map((b) => [b, MARKDOWN])) as Record<Block, Render>,
	preview: {} as Record<string, string>
});
