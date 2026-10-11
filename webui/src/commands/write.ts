import type { Command } from '../lib/types.js';
import { RUNNING_CONFIG, STARTUP_CONFIG, beyond } from '../lib/config.js';
import copy from './copy.js';

const MEMORY = 'memory';

// write memory, IOS's way to save, write alone the same: the copy of the
// running-config to the startup-config, under the same rules as copy
export default {
	path: ['write'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (args.length > 1) throw beyond(['write', args[0]], args.slice(1));
		if (args.length === 1 && !MEMORY.startsWith(args[0].toLowerCase())) {
			throw new Error(`unknown write "${args[0]}", use ${MEMORY}`);
		}
		return [RUNNING_CONFIG, STARTUP_CONFIG];
	},
	run: copy.run,
	complete(_ctx, args) {
		return args.length === 0 ? [MEMORY] : [];
	}
} satisfies Command<string[]>;
