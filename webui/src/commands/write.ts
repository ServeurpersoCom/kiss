import type { Command } from '../engine/engine.js';
import { RUNNING_CONFIG, STARTUP_CONFIG, beyond } from '../engine/text.js';
import copy from './copy.js';
import erase from './erase.js';
import { choose } from '../engine/near.js';

const MEMORY = 'memory';
const ERASE = 'erase';

// write memory, IOS's way to save, write alone the same: the copy of the
// running-config to the startup-config, under the same rules as copy; write
// erase, IOS's erase startup-config, under the rules of erase. The plan is
// what copy takes, a configuration to another, or the one erase takes
export default {
	path: ['write'],
	roles: ['user', 'llm'],
	alone: true,
	parse(_schema, args) {
		if (args.length > 1) throw beyond(['write', args[0]], args.slice(1));
		if (args.length === 1 && choose(args[0], [MEMORY, ERASE], 'write') === ERASE)
			return [STARTUP_CONFIG];
		return [RUNNING_CONFIG, STARTUP_CONFIG];
	},
	run(ctx, plan) {
		return plan.length === 1 ? erase.run(ctx, plan[0]) : copy.run(ctx, plan);
	},
	complete(_ctx, args) {
		return args.length === 0 ? [MEMORY, ERASE] : [];
	}
} satisfies Command<string[]>;
