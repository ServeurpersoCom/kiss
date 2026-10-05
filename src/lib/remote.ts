import type { ConfigReader } from './types.js';

// an item of a collection module that names a remote server by a url, an
// optional bearer key and the seconds it has to answer: endpoints and mcp
export interface Remote {
	name: string;
	url: string;
	key: string;
	timeout: number;
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
			timeout: Number(config.get(`${module} timeout`, name))
		}));
}

// the rule of such a module: every item has a url
export function bare(config: ConfigReader, module: string): string | null {
	const names = config.names(module).filter((n) => !config.get(`${module} url`, n));
	return names.length ? `${module} ${names.join(' ')} has no url` : null;
}

export function bearer(key: string): Record<string, string> {
	return key ? { Authorization: `Bearer ${key}` } : {};
}
