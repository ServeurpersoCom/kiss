import type { Module } from '../engine/engine.js';
import { rules } from '../lib/remote.js';
import { aggregate, forget, routed } from '../lib/mcp.js';
import { display } from '../rendering/display.svelte.js';
import { tools } from '../lib/tools.js';
import { follow } from '../lib/sync.js';

export default {
	name: 'mcp',
	keys: {
		// a Streamable HTTP endpoint, the MCP url of the server
		url: { kind: 'url', named: true, guard: true },
		key: { kind: 'secret', named: true },
		// the header that carries the key, Header or Header: Scheme, a bearer
		// token unless set
		'auth-header': { kind: 'string', named: true },
		// sent with every request, Name: value pairs split by ;
		headers: { kind: 'string', named: true },
		// seconds a call of a tool may take, until the turn stops at the latest
		timeout: { kind: 'number', min: 0.1, default: '310', named: true },
		// the KiSS server of another item whose route of the same name this one
		// goes through, reached at proxy/<name> beside its url, with its key and
		// headers, the url and key of this one waiting until it goes direct
		proxy: { kind: 'string', named: true, guard: true, of: 'mcp' }
	},
	validate(config) {
		return rules(config, 'mcp');
	},
	verify(config, before, source) {
		return routed('mcp', config, before, source);
	},
	// every server connects as soon as the configuration names it, so the first
	// round that needs its tools finds them listed, the thread reading the item
	// of each tool by the name it was called by, and one no longer named is
	// forgotten; the page follows its backend wherever it goes
	apply(config) {
		forget(config);
		void aggregate(config, tools, new Map()).then(({ items }) => (display.items = items));
		follow(config);
	},
	// every server answers with its tools, each under a name the model takes
	async check(config) {
		const { problems } = await aggregate(config, tools, new Map());
		return problems.length ? problems.join(', ') : null;
	}
} satisfies Module;
