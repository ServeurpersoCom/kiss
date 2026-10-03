import type { Module } from '../lib/types.js';
import { listModels } from '../lib/api.js';
import { bare, remotes } from '../lib/remote.js';

export default {
	name: 'endpoints',
	keys: {
		url: { kind: 'url', named: true },
		key: { kind: 'secret', named: true }
	},
	validate(config) {
		return bare(config, 'endpoints');
	},
	// every endpoint lists its models
	async check(config) {
		const failures = await Promise.all(
			remotes(config, 'endpoints').map((e) =>
				listModels(e.url, e.key).then(
					() => null,
					(err: Error) => `endpoints ${e.name}: ${err.message}`
				)
			)
		);
		const failed = failures.filter((f) => f !== null);
		return failed.length ? failed.join(', ') : null;
	}
} satisfies Module;
