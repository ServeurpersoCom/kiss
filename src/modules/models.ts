import type { Module } from '../lib/types.js';
import { listModels } from '../lib/api.js';
import { remotes } from '../lib/remote.js';
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
	// the models of every endpoint at once, by endpoint
	async items(ctx) {
		return Promise.all(
			remotes(ctx.running, 'endpoints').map((e) =>
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
