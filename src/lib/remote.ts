import type { ConfigReader, Image } from './types.js';

// an item of a collection module that names a remote server by a url, an
// optional bearer key, the headers sent besides it and the seconds it has to
// answer: endpoints and mcp
export interface Remote {
	name: string;
	url: string;
	key: string;
	headers: Record<string, string>;
	timeout: number;
}

// the headers of an item, Name: value pairs split by ;
const SEPARATOR = ';';
const TOKEN = /^[!#$%&'*+.^_`|~0-9A-Za-z-]+$/;

// the pairs of a headers value, each a name and its value, either empty when
// the pair is no Name: value
function pairs(text: string): [string, string][] {
	return text
		.split(SEPARATOR)
		.map((p) => p.trim())
		.filter((p) => p)
		.map((p) => {
			const colon = p.indexOf(':');
			const name = colon < 0 ? '' : p.slice(0, colon).trim();
			return TOKEN.test(name) ? [name, p.slice(colon + 1).trim()] : ['', p];
		});
}

// every item of the module that has a url, sorted by name
export function remotes(config: ConfigReader, module: string): Remote[] {
	return config
		.names(module)
		.filter((name) => config.get(`${module} url`, name))
		.map((name) => ({
			name,
			url: String(config.get(`${module} url`, name)),
			key: String(config.get(`${module} key`, name) ?? ''),
			headers: Object.fromEntries(pairs(String(config.get(`${module} headers`, name) ?? ''))),
			timeout: Number(config.get(`${module} timeout`, name))
		}));
}

// the rules of such a module: every item has a url, and its headers are
// Name: value pairs
export function rules(config: ConfigReader, module: string): string | null {
	const names = config.names(module).filter((n) => !config.get(`${module} url`, n));
	if (names.length) return `${module} ${names.join(' ')} has no url`;
	for (const name of config.names(module)) {
		const text = String(config.get(`${module} headers`, name) ?? '');
		const bad = pairs(text).find(([n, v]) => !n || !v);
		if (bad) return `${module} headers ${name}: "${bad[1] || bad[0]}" is not Name: value`;
	}
	return null;
}

// a key as a bearer token, none without a key
export function bearer(key: string): Record<string, string> {
	return key ? { Authorization: `Bearer ${key}` } : {};
}

// what a request to an MCP server carries: its key as a bearer token, then its
// headers, which may override it
export function headers(remote: Remote): Record<string, string> {
	return { ...bearer(remote.key), ...remote.headers };
}

// an image as a URL that holds it, for a protocol that takes images so
export const dataUrl = (image: Image): string => `data:${image.mime};base64,${image.data}`;
