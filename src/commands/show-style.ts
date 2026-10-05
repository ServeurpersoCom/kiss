import type { Command } from '../lib/types.js';

// the rules of the page style, one per line, those of the libraries left out
// as their own layer holds them, the scoping classes of Svelte too as no
// selector needs them
function rules(list: CSSRuleList, page: boolean): string[] {
	return [...list].flatMap((rule) => {
		if (rule instanceof CSSLayerBlockRule && rule.name === 'page')
			return rules(rule.cssRules, true);
		if (!page) return [];
		return [rule.cssText.replace(/\.svelte-[a-z0-9]+/g, '').replace(/\s+/g, ' ')];
	});
}

export default {
	path: ['show', 'style'],
	roles: ['user', 'llm'],
	// the style of the page, which css sheets restyle
	run() {
		return [...document.styleSheets].flatMap((s) => rules(s.cssRules, false)).join('\n');
	}
} satisfies Command;
