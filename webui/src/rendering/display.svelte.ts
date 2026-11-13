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
type Block = (typeof BLOCKS)[number];

// how the thread shows a turn: how the thinking and the tool calls fold, how
// each block of text renders and the argument every folded call shows, from
// the display module, the argument a tool shows instead, by its item, from
// the tools module, and the item of each tool by the name the model calls it
// by, as the tools were last served
export const display = $state({
	thinking: INFERENCE as Fold,
	tools: INFERENCE as Fold,
	rendering: Object.fromEntries(BLOCKS.map((b) => [b, MARKDOWN])) as Record<Block, Render>,
	argument: '',
	preview: {} as Record<string, string>,
	items: {} as Record<string, string>
});

// the argument a folded call of a tool shows, by the name the model called it
// by: the one the tools module names for its item, else the one the display
// names for every tool
export function argument(name: string): string {
	return display.preview[display.items[name] ?? name] ?? display.argument;
}
