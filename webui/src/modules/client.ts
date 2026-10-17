import type { Module } from '../engine/engine.js';
import { HOSTNAME_STORAGE_KEY } from '../lib/config.js';
import { follow } from '../lib/sync.js';

// a hostname takes a-z, 0-9 and _, as a tool name does, and leaves room for
// _client_config within the 64 characters of a tool name
const HOSTNAME = /^[a-z0-9_]{1,50}$/;
const LETTERS = 'abcdefghijklmnopqrstuvwxyz';

// four letters drawn the first time the page opens, kept by the browser
function drawn(): string {
	const kept = localStorage.getItem(HOSTNAME_STORAGE_KEY);
	if (kept) return kept;
	const name = Array.from(crypto.getRandomValues(new Uint8Array(4)), (b) => LETTERS[b % 26]).join(
		''
	);
	localStorage.setItem(HOSTNAME_STORAGE_KEY, name);
	return name;
}

// the page as a machine of KiSS: the hostname its server calls its config
// tool by, <hostname>_client_config, while the page is attached
export default {
	name: 'client',
	keys: {
		hostname: { kind: 'string', default: drawn() }
	},
	validate(config) {
		const name = String(config.get('client hostname') ?? '');
		return HOSTNAME.test(name)
			? null
			: `client hostname "${name}" takes a-z, 0-9 and _ only, 50 at most`;
	},
	apply(config) {
		follow(config);
	}
} satisfies Module;
