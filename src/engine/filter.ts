import { tokenize } from './parse.js';
import { INDENT } from '../lib/config.js';

type Lines = (lines: string[]) => string[];

// output filters after a pipe, IOS style: show style | include :root; every
// filter but count takes a pattern, a case blind regular expression; a
// section is a line at the margin with the indented lines under it, kept
// whole when that line matches: show tools | section sandbox
const PATTERNED: Record<string, (re: RegExp) => Lines> = {
	begin: (re) => (lines) => {
		const i = lines.findIndex((l) => re.test(l));
		return i < 0 ? [] : lines.slice(i);
	},
	exclude: (re) => (lines) => lines.filter((l) => !re.test(l)),
	include: (re) => (lines) => lines.filter((l) => re.test(l)),
	section: (re) => (lines) => {
		let kept = false;
		return lines.filter((l) => (l.startsWith(INDENT) ? kept : (kept = re.test(l))));
	}
};

const count: Lines = (lines) => [String(lines.length)];

const NAMES = [...Object.keys(PATTERNED), 'count'].sort();

// the filter a pipe spells, compiled once: a bad name or pattern fails here,
// before anything runs
export function compileFilter(spec: string): (text: string) => string {
	const [word = '', ...rest] = tokenize(spec);
	const names = NAMES.filter((n) => n.startsWith(word.toLowerCase()));
	if (names.length !== 1) throw new Error(`unknown filter "${word}", use ${NAMES.join(' ')}`);
	const name = names[0];
	let lines: Lines;
	if (name === 'count') {
		if (rest.length) throw new Error(`count takes no pattern: ${rest.join(' ')}`);
		lines = count;
	} else {
		if (!rest.length) throw new Error(`${name} needs a pattern`);
		let re: RegExp;
		try {
			re = new RegExp(rest.join(' '), 'i');
		} catch {
			throw new Error(`"${rest.join(' ')}" is not a pattern`);
		}
		lines = PATTERNED[name](re);
	}
	return (text) => lines(text ? text.split('\n') : []).join('\n');
}
