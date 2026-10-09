import type { Module } from '../lib/types.js';
import { ASK, LEVELS } from '../lib/types.js';
import { PRIVILEGE } from '../lib/config.js';

// the modules holding a key whose change by the model is guarded, but this
// one: a privilege never rules itself
function guarded(modules: readonly Module[]): string[] {
	return modules
		.filter((m) => m.name !== PRIVILEGE && Object.values(m.keys).some((k) => k.guard))
		.map((m) => m.name);
}

// how far the model changes the guarded keys of each module: deny, never; ask,
// once the user agrees; allow, freely; a change of it by the model always asks
export default {
	name: PRIVILEGE,
	keys: {
		level: { kind: 'enum', values: LEVELS, default: ASK, named: true, guard: 'change' }
	},
	validate(config, modules) {
		const all = guarded(modules);
		const stray = config.names(PRIVILEGE).filter((n) => !all.includes(n));
		return stray.length ? `${stray.join(' ')} takes no privilege` : null;
	},
	// every module holding a guarded key, with its privilege
	async items(ctx) {
		return [{ group: 'modules', names: guarded(ctx.modules) }];
	}
} satisfies Module;
