import type { Module } from '../lib/types.js';
import { listModels, pick } from '../lib/api.js';

export default {
	name: 'chat',
	keys: {
		// endpoint/model as show models lists it; empty: the one model of the one endpoint
		model: { kind: 'string', default: '' },
		// sent first in every request, none when empty
		system: { kind: 'string', default: '' }
	},
	async check(config) {
		if (!config.get('chat model')) return null;
		const { endpoint, model } = await pick(config);
		const served = await listModels(endpoint);
		return served.includes(model)
			? null
			: `${endpoint.name} does not serve ${model}, see show models`;
	}
} satisfies Module;
