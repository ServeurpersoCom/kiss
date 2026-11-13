import type { Command } from '../../engine/engine.js';
import { comment } from '../../engine/text.js';
import { NEWLINE, PAGE_LAYER, SPACE } from '../../lib/config.js';

// the rules of the page style, one per line, those of the libraries left out
// as their own layer holds them
function rules(list: CSSRuleList, page: boolean): string[] {
	return [...list].flatMap((rule) => {
		if (rule instanceof CSSLayerBlockRule && rule.name === PAGE_LAYER)
			return rules(rule.cssRules, true);
		if (!page) return [];
		return [rule.cssText.replace(/\s+/g, SPACE)];
	});
}

export default {
	path: ['show', 'css'],
	roles: ['user', 'llm'],
	// the CSS of the page by component, a rule under the component its scoping
	// class names, svelte-composer for Composer.svelte, the rules of no
	// component under page: a style sheet takes a selector as written there to
	// aim at that component alone
	run() {
		const groups = new Map<string, string[]>();
		for (const rule of [...document.styleSheets].flatMap((s) => rules(s.cssRules, false))) {
			const name = /\bsvelte-([a-z0-9]+)/.exec(rule)?.[1] ?? PAGE_LAYER;
			groups.set(name, [...(groups.get(name) ?? []), rule]);
		}
		return [...groups].flatMap(([name, list]) => [comment(name), ...list]).join(NEWLINE);
	}
} satisfies Command;
