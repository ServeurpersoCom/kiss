import type { Module } from '../lib/types.js';
import { EndpointError, endpoints, listModels, pick } from '../lib/api.js';

export default {
	name: 'chat',
	keys: {
		// endpoint/model as show models lists it; empty: the one model of the one endpoint
		model: { kind: 'string', default: '', guard: 'change' },
		// sent first in every request, none when empty
		system: { kind: 'string', default: '', guard: 'change' }
	},
	// the model a turn would talk to is served; a page without an endpoint has
	// no chat to check, and an endpoint that fails is the warning of endpoints
	async check(config) {
		if (!endpoints(config).length) return null;
		try {
			const { endpoint, model } = await pick(config);
			const served = await listModels(endpoint);
			return served.includes(model)
				? null
				: `${endpoint.name} does not serve ${model}, see show models`;
		} catch (e) {
			if (e instanceof EndpointError) return null;
			throw e;
		}
	}
} satisfies Module;
