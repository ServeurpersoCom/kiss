import { TYPO_LETTERS_PER_EDIT } from '../lib/config.js';

// insertions, deletions and substitutions turning a into b
function distance(a: string, b: string): number {
	let prev = Array.from({ length: b.length + 1 }, (_, j) => j);
	for (let i = 1; i <= a.length; i++) {
		const row = [i];
		for (let j = 1; j <= b.length; j++) {
			const cost = a[i - 1] === b[j - 1] ? 0 : 1;
			row[j] = Math.min(prev[j] + 1, row[j - 1] + 1, prev[j - 1] + cost);
		}
		prev = row;
	}
	return prev[b.length];
}

// the candidate closest to the word, when close enough to be a typo of it
export function nearest(word: string, candidates: readonly string[]): string | undefined {
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

// ", did you mean x" when a candidate is close, else nothing
export function didYouMean(word: string, candidates: readonly string[]): string {
	const near = nearest(word, candidates);
	return near ? `, did you mean ${near}` : '';
}
