import { COMMENT_PREFIX, PIPE } from '../lib/config.js';

// one command per line; blank lines and comments are skipped
export function splitLines(text: string): string[] {
	return text
		.split('\n')
		.map((line) => line.trim())
		.filter((line) => line && !line.startsWith(COMMENT_PREFIX));
}

// the JSON escapes; any other backslash stays as written, so a pattern such as
// "\\d+" needs no doubling
const ESCAPES: Record<string, string> = {
	'"': '"',
	'\\': '\\',
	'/': '/',
	b: '\b',
	f: '\f',
	n: '\n',
	r: '\r',
	t: '\t'
};

function blank(c: string): boolean {
	return c === ' ' || c === '\t';
}

// splits a line on blanks; a token opening with a double quote takes JSON
// escapes and one opening with a single quote is literal, so every value
// printed by the schema reads back unchanged
export function tokenize(line: string): string[] {
	const tokens: string[] = [];
	let i = 0;
	while (i < line.length) {
		const c = line[i];
		if (blank(c)) {
			i++;
			continue;
		}
		if (c === '"') {
			let token = '';
			let j = i + 1;
			for (; j < line.length && line[j] !== '"'; j++) {
				if (line[j] !== '\\' || j + 1 >= line.length) {
					token += line[j];
					continue;
				}
				const e = line[++j];
				if (e === 'u' && /^[0-9a-fA-F]{4}$/.test(line.slice(j + 1, j + 5))) {
					token += String.fromCharCode(parseInt(line.slice(j + 1, j + 5), 16));
					j += 4;
				} else {
					token += ESCAPES[e] ?? '\\' + e;
				}
			}
			if (j >= line.length) throw new Error('unterminated "');
			tokens.push(token);
			i = j + 1;
			continue;
		}
		if (c === "'") {
			const j = line.indexOf("'", i + 1);
			if (j < 0) throw new Error("unterminated '");
			tokens.push(line.slice(i + 1, j));
			i = j + 1;
			continue;
		}
		let j = i;
		while (j < line.length && !blank(line[j])) j++;
		tokens.push(line.slice(i, j));
		i = j;
	}
	return tokens;
}

// splits a line at its first pipe outside quotes, as IOS does: the command,
// then the filter, the rest of the line as written, pipes included
export function splitFilter(line: string): [string, string | undefined] {
	let quote = '';
	for (let i = 0; i < line.length; i++) {
		const c = line[i];
		if (quote) {
			if (c === '\\' && quote === '"') i++;
			else if (c === quote) quote = '';
		} else if (c === '"' || c === "'") {
			quote = c;
		} else if (c === PIPE) {
			return [line.slice(0, i).trim(), line.slice(i + 1)];
		}
	}
	return [line.trim(), undefined];
}
