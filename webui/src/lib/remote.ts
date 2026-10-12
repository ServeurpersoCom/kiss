import type { ConfigReader, Image } from './types.js';

// an item of a collection module that names a remote server by a url, an
// optional key, the headers sent besides it and the seconds it has to answer:
// endpoints and mcp; proxied, it goes through a KiSS server, the key and the
// headers those of that server
export interface Remote {
	name: string;
	url: string;
	key: string;
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

// an item as it names its server: its url, key, headers and seconds
function own(config: ConfigReader, module: string, name: string): Remote {
	return {
		name,
		url: String(config.get(`${module} url`, name)),
		key: String(config.get(`${module} key`, name) ?? ''),
		headers: Object.fromEntries(pairs(String(config.get(`${module} headers`, name) ?? ''))),
		timeout: Number(config.get(`${module} timeout`, name)),
		proxied: false
	};
}

// every item of the module that has a url, sorted by name; an item with a
// proxy, an MCP server or an endpoint alike, reaches its server through the
// proxy of the KiSS server the proxy names, at proxy/<name> beside its MCP,
// with the key and headers of that server, in its own seconds
export function remotes(config: ConfigReader, module: string): Remote[] {
	return config
		.names(module)
		.filter((name) => config.get(`${module} url`, name))
		.map((name) => {
			const proxy = config.get(`${module} proxy`, name);
			if (proxy === undefined) return own(config, module, name);
			const server = own(config, SERVERS, proxy);
			return {
				...server,
				name,
				url: new URL(`${PROXY}/${name}`, new URL(server.url, location.href)).href,
				timeout: own(config, module, name).timeout,
				proxied: true
			};
		});
}

// the rules of such a module: every item has a url of its own, a proxy names
// a KiSS server, another item of mcp that has no proxy itself, and no server
// is reached twice, a relative url resolved against the page, since the same
// server reached twice serves the same names twice; its headers are Name:
// value pairs
export function rules(config: ConfigReader, module: string): string | null {
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
