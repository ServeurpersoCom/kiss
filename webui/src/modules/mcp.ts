import type { Module } from '../lib/types.js';
import { rules } from '../lib/remote.js';
import { aggregate, served } from '../lib/mcp.js';
import { tools } from '../lib/tools.js';

export default {
	name: 'mcp',
	keys: {
		// a Streamable HTTP endpoint, the MCP url of the server
		url: { kind: 'url', named: true, guard: true },
		// sent as a bearer token
		key: { kind: 'secret', named: true },
		// sent with every request, Name: value pairs split by ;
		headers: { kind: 'string', named: true },
		// seconds a call of a tool may take, until the turn stops at the latest
		timeout: { kind: 'number', min: 0.1, default: '300', named: true }
	},
	validate(config) {
		return rules(config, 'mcp');
	},
	// every server connects as soon as the configuration names it, so the first
	// round that needs its tools finds them listed
	apply(config) {
		void served(config, new Map());
	},
	// every server answers with its tools, and no name is served twice
	async check(config) {
		const { problems } = await aggregate(config, tools, new Map());
		return problems.length ? problems.join(', ') : null;
	}
} satisfies Module;
