// how the thread shows a turn: whether the thinking and the tool calls open
// unfolded, from the display module, and the argument each tool shows folded,
// by tool name, from the tool module
export const display = $state({
	thinking: false,
	tools: false,
	preview: {} as Record<string, string>
});
