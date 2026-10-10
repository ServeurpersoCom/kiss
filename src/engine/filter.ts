type Lines = (lines: string[]) => string[];

// the filter after a pipe, IOS style: show css | include :root; every filter
// but count takes a pattern, the rest of the line as written, a case blind
// regular expression: include ^set (tools|style)
const PATTERNED: Record<string, (re: RegExp) => Lines> = {
	begin: (re) => (lines) => {
		const i = lines.findIndex((l) => re.test(l));
		return i < 0 ? [] : lines.slice(i);
	},
	exclude: (re) => (lines) => lines.filter((l) => !re.test(l)),
	include: (re) => (lines) => lines.filter((l) => re.test(l))
};

const count: Lines = (lines) => [String(lines.length)];

const NAMES = [...Object.keys(PATTERNED), 'count'].sort();

// the filter a pipe spells, compiled once: a bad name or pattern fails here,
// before anything runs
export function compileFilter(spec: string): (text: string) => string {
	const trimmed = spec.trim();
	const word = trimmed.split(/\s/, 1)[0];
	const pattern = trimmed.slice(word.length).trim();
	const names = NAMES.filter((n) => n.startsWith(word.toLowerCase()));
	if (names.length !== 1) throw new Error(`unknown filter "${word}", use ${NAMES.join(' ')}`);
	const name = names[0];
	let lines: Lines;
	if (name === 'count') {
		if (pattern) throw new Error(`count takes no pattern: ${pattern}`);
		lines = count;
	} else {
		if (!pattern) throw new Error(`${name} needs a pattern`);
		let re: RegExp;
		try {
			re = new RegExp(pattern, 'i');
		} catch {
			throw new Error(`"${pattern}" is not a pattern`);
		}
		lines = PATTERNED[name](re);
	}
	return (text) => lines(text ? text.split('\n') : []).join('\n');
}
