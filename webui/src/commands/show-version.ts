import type { Command } from '../lib/types.js';
import { SYSTEM, comment } from '../lib/config.js';
import { served } from '../lib/mcp.js';

// the system, then the page, then every KiSS server the page reaches, each
// with the line it gives of itself, as a router lists its components
export default {
	path: ['show', 'version'],
	roles: ['user', 'llm'],
	async run(ctx) {
		const servers = (await served(ctx.running, new Map())).flatMap((s) =>
			s.system ? [s.system] : []
		);
		return [SYSTEM, `page ${__KISS_VERSION__}`, ...servers].map(comment).join('\n');
	}
} satisfies Command;
