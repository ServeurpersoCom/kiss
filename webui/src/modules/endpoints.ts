import type { Module } from '../engine/engine.js';
import { DEFAULT_PROTOCOL, PROTOCOLS, endpoints, fallback, listModels } from '../lib/api.js';
import { rules } from '../lib/remote.js';
import { routed } from '../lib/mcp.js';

export default {
	name: 'endpoints',
	keys: {
		url: { kind: 'url', named: true, guard: true },
		// how the endpoint speaks, a file of protocols/ by its name
		protocol: { kind: 'enum', values: PROTOCOLS, default: DEFAULT_PROTOCOL, named: true },
		key: { kind: 'secret', named: true },
		// the header that carries the key, Header or Header: Scheme, the one of
		// the protocol unless set
		'auth-header': { kind: 'string', named: true },
		// sent with every request, Name: value pairs split by ;
		headers: { kind: 'string', named: true },
		// seconds the endpoint has to start answering: a model list, the start of
		// a reply, which then streams until it ends or the turn stops
		timeout: { kind: 'number', min: 0.1, default: '120', named: true },
		// the times a reply is asked again while the endpoint answers a status
		// that passes, 0 never
		'max-retries': { kind: 'number', min: 0, integer: true, default: '1800', named: true },
		// seconds between two of them
		'retry-pause': { kind: 'number', min: 0, default: '2', named: true },
		// the KiSS server, an item of mcp, whose route of the same name this one
		// goes through, reached at proxy/<name> beside its MCP, with its key and
		// headers, the url and key of this one waiting until it goes direct
		proxy: { kind: 'string', named: true, guard: true, of: 'mcp' }
	},
	validate(config) {
		return rules(config, 'endpoints', (name) => fallback(config, name));
	},
	verify(config, before, source) {
		return routed('endpoints', config, before, source);
	},
	// every endpoint lists its models
	async check(config) {
		const failures = await Promise.all(
			endpoints(config).map((e) =>
				listModels(e).then(
					() => null,
					(err: Error) => `endpoints ${e.name}: ${err.message}`
				)
			)
		);
		const failed = failures.filter((f) => f !== null);
		return failed.length ? failed.join(', ') : null;
	}
} satisfies Module;
