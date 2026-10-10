import type { Module } from '../lib/types.js';
import { endpoints, listModels } from '../lib/api.js';
import { MODEL_SEPARATOR } from '../lib/config.js';

// every setting of a model, the model named as the item endpoint/model as chat
// model takes it: the parameters of an OpenAI compatible request under their
// own names, each sent only when set
export default {
	name: 'models',
	keys: {
		temperature: { kind: 'number', min: 0, named: true },
		top_p: { kind: 'number', min: 0, max: 1, named: true },
		top_k: { kind: 'number', min: 0, integer: true, named: true },
		min_p: { kind: 'number', min: 0, max: 1, named: true },
		max_tokens: { kind: 'number', min: 1, integer: true, named: true },
		presence_penalty: { kind: 'number', min: -2, max: 2, named: true },
		frequency_penalty: { kind: 'number', min: -2, max: 2, named: true },
		seed: { kind: 'number', integer: true, named: true },
		// as the template of the model reads it: low, medium, high or its own
		reasoning_effort: { kind: 'string', named: true }
	},
	// a parameter set for a model whose endpoint speaks a protocol that never
	// sends it is said once, the model left as set
	async check(config) {
		const all = endpoints(config);
		const unsent = config.names('models').flatMap((item) => {
			const endpoint = all.find((e) => item.startsWith(e.name + MODEL_SEPARATOR));
			if (!endpoint) return [];
			return endpoint.protocol.drops
				.filter((key) => config.get(`models ${key}`, item) !== undefined)
				.map((key) => `models ${key} ${item} is not sent by ${endpoint.protocol.name}`);
		});
		return unsent.length ? unsent.join(', ') : null;
	},
	// the models of every endpoint at once, by endpoint
	async items(ctx) {
		return Promise.all(
			endpoints(ctx.running).map((e) =>
				listModels(e, ctx.signal).then(
					(ids) => ({
						group: `endpoints ${e.name}`,
						names: ids.map((id) => e.name + MODEL_SEPARATOR + id)
					}),
					(err: Error) => ({ group: `endpoints ${e.name}`, names: [], error: err.message })
				)
			)
		);
	}
} satisfies Module;
