import type { Module } from '../lib/types.js';
import { DEFAULT_PROTOCOL, PROTOCOLS, endpoints, listModels } from '../lib/api.js';
import { rules } from '../lib/remote.js';

export default {
	name: 'endpoints',
	keys: {
		url: { kind: 'url', named: true, guard: true },
		// how the endpoint speaks, a file of protocols/ by its name
		protocol: { kind: 'enum', values: PROTOCOLS, default: DEFAULT_PROTOCOL, named: true },
		key: { kind: 'secret', named: true },
		// sent with every request, Name: value pairs split by ;
		headers: { kind: 'string', named: true },
		// seconds the endpoint has to start answering: a model list, the start of
		// a reply, which then streams until it ends or the turn stops
		timeout: { kind: 'number', min: 0.1, default: '120', named: true },
		// the times a reply is asked again while the endpoint answers a status
		// that passes, 0 never
		retries: { kind: 'number', min: 0, integer: true, default: '10', named: true },
		// seconds between two of them, unless the endpoint asks for a wait the
		// retries and their pauses hold in all
		pause: { kind: 'number', min: 0, default: '2', named: true }
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
