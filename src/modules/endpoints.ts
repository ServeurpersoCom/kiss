import type { Module } from '../lib/types.js';
import { listModels } from '../lib/api.js';
import { bare, remotes } from '../lib/remote.js';

export default {
	name: 'endpoints',
	keys: {
		url: { kind: 'url', named: true },
		key: { kind: 'secret', named: true },
		// seconds the endpoint has to start answering: a model list, the start of
		// a reply, which then streams until it ends or the turn stops
		timeout: { kind: 'number', min: 0.1, default: '10', named: true }
	},
	validate(config) {
		return bare(config, 'endpoints');
	},
	// every endpoint lists its models
	async check(config) {
		const failures = await Promise.all(
			remotes(config, 'endpoints').map((e) =>
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
