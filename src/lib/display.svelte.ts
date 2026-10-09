// how a block folds: folded, unfolded, or unfolded while the model writes it only
export const FOLDS = ['closed', 'open', 'inference'] as const;
export type Fold = (typeof FOLDS)[number];
export const [CLOSED, OPEN, INFERENCE] = FOLDS;

// how the thread shows a turn: how the thinking and the tool calls fold, from
// the display module, and the argument each tool shows folded, by tool name,
// from the tools module
export const display = $state({
	thinking: CLOSED as Fold,
	tools: CLOSED as Fold,
	preview: {} as Record<string, string>
});
