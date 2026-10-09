import type { Module } from '../lib/types.js';
import { ASK, LEVELS } from '../lib/types.js';
import { PRIVILEGE } from '../lib/config.js';

// the modules holding a key whose change by the model is guarded
function guarded(modules: readonly Module[]): string[] {
	return modules.filter((m) => Object.values(m.keys).some((k) => k.guard)).map((m) => m.name);
}

// how far the model changes the guarded keys of each module: deny, never; ask,
// once the user agrees; allow, freely; only the user sets it
export default {
	name: PRIVILEGE,
	user: true,
	keys: {
		level: { kind: 'enum', values: LEVELS, default: ASK, named: true }
	},
	validate(config, modules) {
		const all = guarded(modules);
		const stray = config.names(PRIVILEGE).filter((n) => !all.includes(n));
		return stray.length ? `${stray.join(' ')} guards no key` : null;
	},
	// every module holding a guarded key, with its privilege
	async items(ctx) {
		return [{ group: 'modules', names: guarded(ctx.modules) }];
	}
} satisfies Module;
