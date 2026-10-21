import { REMOVED } from './text.js';

// the seal of the keychain: every secret it is given read <removed>, in a text
// as it is written, and in JSON as decoded, the names of its properties
// included

// a text with every secret replaced by <removed>, written as it is
export function sealText(text: string, all: readonly string[]): string {
	return all.reduce((t, s) => t.split(s).join(REMOVED), text);
}

// a JSON value with every string it holds sealed, the names of its
// properties included
function sealed(value: unknown, all: readonly string[]): unknown {
	if (typeof value === 'string') return sealText(value, all);
	if (Array.isArray(value)) return value.map((v) => sealed(v, all));
	if (value && typeof value === 'object')
		return Object.fromEntries(
			Object.entries(value).map(([k, v]) => [sealText(k, all), sealed(v, all)])
		);
	return value;
}

// a value as JSON, every string it holds sealed as decoded, a name of a
// property as a value, so a secret escaped or streamed in pieces is whole
// there
export function seal(value: unknown, all: readonly string[]): string {
	return JSON.stringify(sealed(JSON.parse(JSON.stringify(value)), all));
}
