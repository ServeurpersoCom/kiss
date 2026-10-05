// how the thread shows a turn: how the thinking and the tool calls fold, from
// the display module, and the argument each tool shows folded, by tool name,
// from the tools module
export const display = $state({
	thinking: 'closed',
	tools: 'closed',
	preview: {} as Record<string, string>
});
