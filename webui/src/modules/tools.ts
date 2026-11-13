import type { Module } from '../engine/engine.js';
import { NAME, TOOL_SEPARATOR } from '../lib/config.js';
import { display } from '../rendering/display.svelte.js';
import { mcpItem, prefixed, served } from '../lib/mcp.js';
import { CONSENT, USES, tools } from '../lib/tools.js';

// every setting of a tool, the tool named as the item: one of KiSS by its
// name, one of an MCP server by the server and its name, exa/search, whatever
// name the model sees it by
export default {
	name: 'tools',
	keys: {
		// the rounds of tool calls a turn takes at most
		'max-rounds': { kind: 'number', min: 1, integer: true, default: '1000' },
		// off, the model never sees the tool; consent, each call asks the user,
		// always turning it on; the factory configuration turns the tools of
		// KiSS on, and their changes ask
		use: { kind: 'enum', values: USES, default: CONSENT, named: true, guard: true },
		// the argument a call of the tool shows folded, by its name, over the
		// one display preview-argument names for every tool
		'preview-argument': { kind: 'string', named: true }
	},
	// the argument a call shows folded, by each name the model calls its tool
	// by: one of KiSS by its own, one of an MCP server by its own or prefixed
	// by its server, a name the tools of KiSS or a server before hold staying
	// theirs
	apply(config) {
		const set = config
			.names('tools')
			.filter((tool) => config.get('tools preview-argument', tool))
			.sort((a, b) => Number(a.includes(TOOL_SEPARATOR)) - Number(b.includes(TOOL_SEPARATOR)));
		const preview: Record<string, string> = {};
		for (const tool of set) {
			const argument = String(config.get('tools preview-argument', tool));
			const at = tool.lastIndexOf(TOOL_SEPARATOR);
			const name = tool.slice(at + 1);
			const called = at < 0 ? [name] : [name, prefixed(tool.slice(0, at), name)];
			for (const each of called) preview[each] ??= argument;
		}
		display.preview = preview;
	},
	// the tools of KiSS, then those of every MCP server, each by its item
	async items(ctx) {
		const mcp = await served(ctx.running, new Map());
		return [
			{ group: NAME, names: tools.map((t) => t.name) },
			...mcp.map((s) => ({
				group: `mcp ${s.server}`,
				names: (s.tools ?? []).map((t) => mcpItem(s.server, t.name)),
				...(s.error ? { error: s.error } : {})
			}))
		];
	}
} satisfies Module;
