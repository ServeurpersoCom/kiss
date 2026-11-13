import type { Context } from './engine.js';
import { walk } from './registry.js';
import { SPACE } from '../lib/config.js';
import { PIPE, hinted } from './text.js';

// what may follow the words, the partial word filtering the candidates: path
// words while the path goes on, then the arguments the command proposes; a
// candidate in <> names what to type and shows only before a word is started
export function complete(ctx: Context, words: string[], partial: string): string[] {
	const w = walk(words, ctx.commands);
	if (w.ambiguous) return [];
	const found = new Set<string>();
	if (w.consumed === words.length) {
		for (const c of w.pool) if (c.path.length > w.consumed) found.add(c.path[w.consumed]);
	}
	if (w.command && (w.consumed === words.length || w.depth === w.consumed)) {
		for (const arg of w.command.complete?.(ctx, words.slice(w.depth)) ?? []) found.add(arg);
	}
	const typed = partial.toLowerCase();
	return [...found]
		.filter((n) => (hinted(n) ? !partial : n.toLowerCase().startsWith(typed)))
		.sort((a, b) => Number(hinted(a)) - Number(hinted(b)) || a.localeCompare(b));
}

// the word a line ends on, after its last blank or pipe, as the console of
// the server reads it
export function partialWord(text: string): string {
	return text.slice(Math.max(text.lastIndexOf(SPACE), text.lastIndexOf(PIPE)) + 1);
}

// the line completed as far as the candidates agree: the whole word and a
// blank for one candidate, their common start for several
export function extend(text: string, found: readonly string[]): string {
	const words = found.filter((f) => !hinted(f));
	if (!words.length) return text;
	const partial = partialWord(text);
	const head = text.slice(0, text.length - partial.length);
	if (words.length === 1) return head + words[0] + SPACE;
	let common = words[0];
	for (const w of words) while (!w.startsWith(common)) common = common.slice(0, -1);
	return common.length > partial.length ? head + common : text;
}
