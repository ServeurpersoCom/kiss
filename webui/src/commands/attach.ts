import type { Command } from '../engine/engine.js';
import { BACKEND, attach } from '../lib/sync.js';

// the page attaches to the KiSS server mcp backend names
export default {
	path: ['attach'],
	roles: ['user'],
	run(ctx) {
		if (!ctx.running.get('mcp url', BACKEND))
			throw new Error(`no mcp url ${BACKEND}, the KiSS server the page attaches to`);
		attach(ctx.running, true);
		return '';
	}
} satisfies Command;
