import type { ConfigReader } from './engine.js';

// the keychain of the page: the one place the value of a key is read, as the
// header of a request leaves, and the one list of every secret the page has
// known, which the seal reads <removed> in what leaves the memory of the page,
// the request to the model, a conversation stored or exported; a secret goes
// out where it must, and nothing kept or sent to the model holds it

// where a key goes when nothing else says: Header, or Header: Scheme
export const BEARER = 'Authorization: Bearer';

const TOKEN = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;

// the secrets the running configuration holds, as the engine lists them
let held: () => string[] = () => [];

// every secret the page has known this session: it only grows, so a key
// replaced while a request runs is still sealed in its answer
const known = new Set<string>();

// the engine names where the secrets of the configuration are read
export function hold(list: () => string[]): void {
	held = list;
}

// a secret the page knows from now on: one the user typed for a server, or
// one a request carries
export function learn(value: string): void {
	if (value) known.add(value);
}

// every secret the page knows, the longest first, so a secret that holds
// another goes whole
export function secrets(): string[] {
	held().forEach(learn);
	return [...known].sort((a, b) => b.length - a.length);
}

// where a key goes, as a header name and its scheme, none when the words are
// no Header or Header: Scheme
export function placement(auth: string): [string, string] | null {
	const colon = auth.indexOf(':');
	const name = (colon < 0 ? auth : auth.slice(0, colon)).trim();
	const scheme = colon < 0 ? '' : auth.slice(colon + 1).trim();
	return TOKEN.test(name) && (!scheme || TOKEN.test(scheme)) ? [name, scheme] : null;
}

// the header that carries the key of an item, placed as auth-header says, none
// without a key: the one read of the value of a key
export function credential(
	config: ConfigReader,
	auth: string,
	module: string,
	name: string
): Record<string, string> {
	const key = String(config.reveal(`${module} key`, name) ?? '');
	learn(key);
	const [header, scheme] = placement(auth) ?? placement(BEARER)!;
	return key ? { [header]: scheme ? `${scheme} ${key}` : key } : {};
}
