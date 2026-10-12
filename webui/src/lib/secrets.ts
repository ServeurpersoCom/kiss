import { REMOVED } from './config.js';

// the shortest value masked as a secret: a shorter one is no key, and would
// mask common text
const SHORTEST = 8;

// the values the user typed in the forms of MCP servers this session, which
// no configuration holds
const typed = new Set<string>();

// a value the user typed for a server, masked from now on
export function learn(value: string): void {
	if (value.length >= SHORTEST) typed.add(value);
}

// every string of a value with each secret replaced by <removed>, the longest
// first, so a secret that holds another goes whole
export function scrub<T>(value: T, secrets: readonly string[]): T {
	const all = [...new Set([...secrets, ...typed])]
		.filter((s) => s.length >= SHORTEST)
		.sort((a, b) => b.length - a.length);
	if (!all.length) return value;
	const walk = (v: unknown): unknown => {
		if (typeof v === 'string') return all.reduce((s, secret) => s.split(secret).join(REMOVED), v);
		if (Array.isArray(v)) return v.map(walk);
		if (v && typeof v === 'object')
			return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, walk(x)]));
		return v;
	};
	return walk(value) as T;
}
