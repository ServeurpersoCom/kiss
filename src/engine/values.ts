import type { Config, Role, Schema, Value } from '../lib/types.js';
import { stored, unstore } from './schema.js';

// a configuration: the keys set in the session, over the values the site gives, over
// the defaults of the schema; the site layer is shared by every copy, and a
// copy written by the model moves no secret: on an item that held one when the
// copy began it changes every key but the url, while unset, drop and load fall
// back to values the user or the site chose; a secret and a url born in the
// same copy go together, the url asking the user
export class Values implements Config {
	private map: Map<string, Value>;
	// the keys set in the session when the copy began
	private before: Map<string, Value>;

	constructor(
		private schema: Schema,
		private site: Map<string, Value>,
		map?: Map<string, Value>,
		private role: Role = 'user'
	) {
		this.map = new Map(map);
		this.before = new Map(map);
	}

	get(key: string, name?: string): Value | undefined {
		const k = stored(key, name);
		if (this.map.has(k)) return this.map.get(k);
		if (this.site.has(k)) return this.site.get(k);
		const def = this.schema.find(key);
		return (name !== undefined ? def?.defaults?.[name] : undefined) ?? def?.default;
	}

	names(module: string): string[] {
		const names = this.stored()
			.map(unstore)
			.filter(([key, name]) => name && key.startsWith(module + ' '))
			.map(([, name]) => name!);
		return [...new Set(names)].sort();
	}

	set(key: string, value: Value, name?: string): void {
		const module = key.split(' ')[0];
		if (this.role === 'llm' && name && this.schema.find(key)?.kind === 'url') {
			const held = this.schema.list().some(([k, def]) => {
				const at = stored(k, name);
				const secret = def.kind === 'secret' && k.startsWith(module + ' ');
				return secret && (this.before.has(at) || this.site.has(at));
			});
			if (held) throw new Error(`${module} ${name} holds a secret, only the user changes its url`);
		}
		this.map.set(stored(key, name), value);
	}

	unset(key: string, name?: string): void {
		this.map.delete(stored(key, name));
	}

	drop(module: string, name: string): void {
		for (const k of [...this.map.keys()]) {
			const [key, item] = unstore(k);
			if (item === name && key.startsWith(module + ' ')) this.map.delete(k);
		}
	}

	stored(): string[] {
		return [...new Set([...this.map.keys(), ...this.site.keys()])].sort();
	}

	values(): Record<string, Value> {
		return Object.fromEntries([...this.map].sort(([a], [b]) => a.localeCompare(b)));
	}

	load(values: Record<string, Value>): string[] {
		const entries = Object.entries(values);
		this.map = new Map(entries.filter(([k]) => this.schema.known(k)));
		return entries.filter(([k]) => !this.schema.known(k)).map(([k]) => k);
	}

	over(values: Record<string, Value>): Record<string, Value> {
		const layer = new Values(this.schema, this.site, new Map(Object.entries(values)));
		return Object.fromEntries(layer.stored().map((k) => [k, layer.get(...unstore(k))!]));
	}

	// a copy written with the rights of a role
	clone(role: Role): Values {
		return new Values(this.schema, this.site, this.map, role);
	}

	// stored keys whose value differs from the other configuration
	changed(other: Values): string[] {
		const keys = new Set([...this.map.keys(), ...other.map.keys()]);
		return [...keys].filter((k) => this.map.get(k) !== other.map.get(k));
	}

	assign(other: Values): void {
		this.map = new Map(other.map);
	}
}
