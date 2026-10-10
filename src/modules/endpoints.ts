import type { Module } from '../lib/types.js';
import { DEFAULT_PROTOCOL, PROTOCOLS, endpoints, listModels } from '../lib/api.js';
import { rules } from '../lib/remote.js';

export default {
	name: 'endpoints',
	keys: {
		url: { kind: 'url', named: true, guard: 'change' },
		// how the endpoint speaks, a file of protocols/ by its name
		protocol: {
			kind: 'enum',
			values: PROTOCOLS,
			default: DEFAULT_PROTOCOL,
			named: true,
			guard: 'change'
		},
		key: { kind: 'secret', named: true },
		// sent with every request, Name: value pairs split by ;
		headers: { kind: 'string', named: true, guard: 'change' },
		// seconds the endpoint has to start answering: a model list, the start of
		// a reply, which then streams until it ends or the turn stops
		timeout: { kind: 'number', min: 0.1, default: '120', named: true }
	},
	validate(config) {
		return rules(config, 'endpoints');
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
