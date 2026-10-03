import type { Command } from '../lib/types.js';
import { NAME } from '../lib/config.js';

export default {
	path: ['show', 'version'],
	roles: ['user', 'llm'],
	run() {
		return `${NAME} ${__KISS_VERSION__}`;
	}
} satisfies Command;
