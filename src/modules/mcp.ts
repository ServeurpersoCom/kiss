import type { Module } from '../lib/types.js';
import { bare } from '../lib/remote.js';
import { aggregate } from '../lib/mcp.js';
import { tools } from '../lib/tools.js';

export default {
	name: 'mcp',
	keys: {
		// a Streamable HTTP endpoint, the MCP url of the server
		url: { kind: 'url', named: true },
		// sent as a bearer token
		key: { kind: 'secret', named: true },
		// seconds a call of a tool may take, until the turn stops at the latest
		timeout: { kind: 'number', min: 0.1, default: '300', named: true }
	},
	validate(config) {
		return bare(config, 'mcp');
	},
	// every server answers with its tools, and no name is served twice
	async check(config) {
		const { problems } = await aggregate(config, tools);
		return problems.length ? problems.join(', ') : null;
	}
} satisfies Module;
