import type { Module } from '../engine/engine.js';
import { EndpointError, endpoints, listModels, pick } from '../lib/api.js';
import { MODEL_SEPARATOR, SLASH } from '../lib/config.js';
import { follow } from '../lib/sync.js';

export default {
	name: 'chat',
	keys: {
		// endpoint/model as show models lists it; empty: the one model of the one endpoint
		model: { kind: 'string', default: '', of: 'models' },
		// sent first in every request, none when empty
		'system-prompt': { kind: 'string', default: '' },
		// the KiSS server, an item of mcp, whose agent answers the conversations
		// opened from now on, as it holds them; empty: the page answers itself
		agent: { kind: 'string', default: '', of: 'mcp' }
	},
	// a model set names an endpoint of the configuration, the model itself
	// left to the endpoint
	validate(config) {
		const agent = String(config.get('chat agent') ?? '');
		if (agent && !config.get('mcp url', agent)) return `chat agent ${agent} names no mcp server`;
		const value = String(config.get('chat model') ?? '');
		if (!value) return null;
		const named = endpoints(config).some((e) => value.startsWith(e.name + MODEL_SEPARATOR));
		return named
			? null
			: `chat model ${value} names no endpoint -> ${SLASH}show models lists every endpoint/model`;
	},
	// the conversations of the server chat agent names follow it
	apply(config) {
		follow(config);
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
