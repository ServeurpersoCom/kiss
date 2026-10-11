import { comment } from '../lib/config.js';

type Lines = (lines: string[]) => string[];

// the filter after a pipe, as IOS reads it: show css | include :root; every
// filter takes a pattern, the rest of the line as written, a regular
// expression that minds case: include ^set (tools|style); count tells how
// many lines match in the words of IOS, as a note
const FILTERS: Record<string, (re: RegExp) => Lines> = {
	begin: (re) => (lines) => {
		const i = lines.findIndex((l) => re.test(l));
		return i < 0 ? [] : lines.slice(i);
	},
	count: (re) => (lines) => [
		comment(`number of lines which match regexp = ${lines.filter((l) => re.test(l)).length}`)
	],
	exclude: (re) => (lines) => lines.filter((l) => !re.test(l)),
	include: (re) => (lines) => lines.filter((l) => re.test(l))
};

const NAMES = Object.keys(FILTERS);

// the filter a pipe spells, compiled once: a bad name or pattern fails here,
// before anything runs
export function compileFilter(spec: string): (text: string) => string {
	const trimmed = spec.trim();
	const word = trimmed.split(/\s/, 1)[0];
	const pattern = trimmed.slice(word.length).trim();
	const names = NAMES.filter((n) => n.startsWith(word.toLowerCase()));
	if (names.length !== 1) throw new Error(`unknown filter "${word}", use ${NAMES.join(' ')}`);
	const name = names[0];
	if (!pattern) throw new Error(`${name} needs a pattern`);
	let re: RegExp;
	try {
		re = new RegExp(pattern);
	} catch {
		throw new Error(`"${pattern}" is not a pattern`);
	}
	const lines = FILTERS[name](re);
	return (text) => lines(text ? text.split('\n') : []).join('\n');
}
