import type { ConfigReader, Image } from './types.js';
import { BEARER, credential, placement } from './keychain.js';

// an item of a collection module that names a remote server by a url, the
// header that carries its key, the headers sent besides it and the seconds it
// has to answer: endpoints and mcp; proxied, it goes through a KiSS server,
// the key and the headers those of that server; sign reads the key through
// the keychain as a request leaves, so no remote holds its value
export interface Remote {
	name: string;
	url: string;
	sign(): Record<string, string>;
	headers: Record<string, string>;
	timeout: number;
	proxied: boolean;
}

// the path of the KiSS server where it proxies a server, beside its MCP
const PROXY = 'proxy';

// the module whose items name the KiSS servers a proxy takes, by their MCP
const SERVERS = 'mcp';

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

// an item as it names its server: its url, key placed by its auth or else as
// fallback says, headers and seconds
function own(config: ConfigReader, module: string, name: string, fallback: string): Remote {
	const auth = String(config.get(`${module} auth`, name) ?? '') || fallback;
	return {
		name,
		url: String(config.get(`${module} url`, name)),
		sign: () => credential(config, auth, module, name),
		headers: Object.fromEntries(pairs(String(config.get(`${module} headers`, name) ?? ''))),
		timeout: Number(config.get(`${module} timeout`, name)),
		proxied: false
	};
}

// every item of the module that has a url, sorted by name; an item with a
// proxy, an MCP server or an endpoint alike, reaches its server through the
// proxy of the KiSS server the proxy names, at proxy/<name> beside its MCP,
// with the key, auth and headers of that server, in its own seconds; fallback
// gives where the key of an item goes when its auth does not say
export function remotes(
	config: ConfigReader,
	module: string,
	fallback: (name: string) => string = () => BEARER
): Remote[] {
	return config
		.names(module)
		.filter((name) => config.get(`${module} url`, name))
		.map((name) => {
			const proxy = config.get(`${module} proxy`, name);
			const item = own(config, module, name, fallback(name));
			if (proxy === undefined) return item;
			const server = own(config, SERVERS, String(proxy), BEARER);
			return {
				...server,
				name,
				url: new URL(`${PROXY}/${name}`, new URL(server.url, location.href)).href,
				timeout: item.timeout,
				proxied: true
			};
		});
}

// the rules of such a module: every item has a url of its own, a proxy names
// a KiSS server, another item of mcp that has no proxy itself, and no server
// is reached twice, a relative url resolved against the page, since the same
// server reached twice serves the same names twice; its auth is Header or
// Header: Scheme, its headers are Name: value pairs, none of them the header
// that carries its key, placed by its auth or else as fallback says
export function rules(
	config: ConfigReader,
	module: string,
	fallback: (name: string) => string = () => BEARER
): string | null {
	const names = config.names(module).filter((n) => !config.get(`${module} url`, n));
	if (names.length) return `${module} ${names.join(' ')} has no url`;
	for (const name of config.names(module)) {
		const proxy = config.get(`${module} proxy`, name);
		if (proxy === undefined) continue;
		if (module === SERVERS && proxy === name)
			return `${module} proxy ${name} names the server itself`;
		if (!config.get(`${SERVERS} url`, proxy))
			return `${module} proxy ${name}: no ${SERVERS} ${proxy}`;
		if (config.get(`${SERVERS} proxy`, proxy) !== undefined)
			return `${module} proxy ${name}: ${SERVERS} ${proxy} has a proxy itself`;
	}
	const owner = new Map<string, string>();
	for (const remote of remotes(config, module)) {
		const url = new URL(remote.url, location.href).href;
		const first = owner.get(url);
		if (first) return `${module} ${remote.name} has the url of ${module} ${first}`;
		owner.set(url, remote.name);
	}
	for (const name of config.names(module)) {
		const text = String(config.get(`${module} headers`, name) ?? '');
		const bad = pairs(text).find(([n, v]) => !n || !v);
		if (bad) return `${module} headers ${name}: "${bad[1] || bad[0]}" is not Name: value`;
		const auth = String(config.get(`${module} auth`, name) ?? '');
		if (auth && !placement(auth))
			return `${module} auth ${name}: "${auth}" is not Header or Header: Scheme`;
		const [header] = placement(auth || fallback(name))!;
		const both = pairs(text).find(([n]) => n.toLowerCase() === header.toLowerCase());
		if (both)
			return `${module} headers ${name}: ${both[0]} is the header ${module} auth ${name} writes`;
	}
	return null;
}

// what a request to an MCP server carries: its key placed by its auth, then
// its headers
export function headers(remote: Remote): Record<string, string> {
	return { ...remote.sign(), ...remote.headers };
}

// an image as a URL that holds it, for a protocol that takes images so
export const dataUrl = (image: Image): string => `data:${image.mime};base64,${image.data}`;
