import { TYPO_LETTERS_PER_EDIT } from '../lib/config.js';

// the edits turning a into b: inserting, deleting or changing a letter, or
// swapping two letters side by side, each one edit
function distance(a: string, b: string): number {
	let before: number[] = [];
	let prev = Array.from({ length: b.length + 1 }, (_, j) => j);
	for (let i = 1; i <= a.length; i++) {
		const row = [i];
		for (let j = 1; j <= b.length; j++) {
			const cost = a[i - 1] === b[j - 1] ? 0 : 1;
			row[j] = Math.min(prev[j] + 1, row[j - 1] + 1, prev[j - 1] + cost);
			if (i > 1 && j > 1 && a[i - 1] === b[j - 2] && a[i - 2] === b[j - 1]) {
				row[j] = Math.min(row[j], before[j - 2] + 1);
			}
		}
		before = prev;
		prev = row;
	}
	return prev[b.length];
}

// the candidate closest to the word, when close enough to be a typo of it
function nearest(word: string, candidates: readonly string[]): string | undefined {
	const max = Math.max(1, Math.floor(word.length / TYPO_LETTERS_PER_EDIT));
	let best: string | undefined;
	let bestDistance = max + 1;
	for (const c of candidates) {
		const d = distance(word.toLowerCase(), c.toLowerCase());
		if (d < bestDistance) {
			best = c;
			bestDistance = d;
		}
	}
	return best;
}

// ", did you mean x" when a candidate is close, else nothing; head, the words
// before the candidate, makes the suggestion a whole one
export function didYouMean(word: string, candidates: readonly string[], head = ''): string {
	const near = nearest(word, candidates);
	return near ? `, did you mean ${head}${near}` : '';
}

// the word a typed word names in a closed set: itself, else the only word it
// starts, case aside; none when it starts none
export function pick(word: string, words: readonly string[], what: string): string | undefined {
	if (!word) return undefined;
	if (words.includes(word)) return word;
	const typed = word.toLowerCase();
	const hits = [...new Set(words.filter((w) => w.toLowerCase().startsWith(typed)))];
	const exact = hits.filter((w) => w.toLowerCase() === typed);
	if (exact.length === 1) return exact[0];
	if (hits.length > 1) throw new Error(`ambiguous ${what} "${word}": ${hits.join(' ')}`);
	return hits[0];
}

// the word a typed word names in a closed set, or why it names none
export function choose(word: string, words: readonly string[], what: string): string {
	const found = pick(word, words, what);
	if (found === undefined) throw new Error(`unknown ${what} "${word}"` + didYouMean(word, words));
	return found;
}
