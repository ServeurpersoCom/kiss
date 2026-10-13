import type { Config, Schema, Value } from '../lib/types.js';
import { REMOVED } from '../lib/config.js';
import { stored, unstore } from './schema.js';

// a configuration: the keys set in the session, over the values the site
// gives, over the factory configuration, over the reset value of each key;
// the site and the factory layers are shared by every copy
export class Values implements Config {
	private map: Map<string, Value>;

	constructor(
		private schema: Schema,
		private site: Map<string, Value>,
		private factory: Map<string, Value>,
		map?: Map<string, Value>
	) {
		this.map = new Map(map);
	}

	get(key: string, name?: string): Value | undefined {
		const value = this.reveal(key, name);
		return value !== undefined && this.schema.find(key)?.kind === 'secret' ? REMOVED : value;
	}

	reveal(key: string, name?: string): Value | undefined {
		const k = stored(key, name);
		if (this.map.has(k)) return this.map.get(k);
		if (this.site.has(k)) return this.site.get(k);
		if (this.factory.has(k)) return this.factory.get(k);
		return this.schema.find(key)?.default;
	}

	names(module: string): string[] {
		const names = [...this.stored(), ...this.factory.keys()]
			.map(unstore)
			.filter(([key, name]) => name && key.startsWith(module + ' '))
			.map(([, name]) => name!);
		return [...new Set(names)].sort();
	}

	set(key: string, value: Value, name?: string): void {
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
		const kept: [string, Value][] = [];
		const dropped: string[] = [];
		for (const [k, v] of Object.entries(values)) {
			const value = this.taken(k, v);
			if (value === undefined) dropped.push(k);
			else kept.push([k, value]);
		}
		this.map = new Map(kept);
		return dropped;
	}

	// a stored value as its key reads it now, none when no module declares the
	// key anymore or the key no longer takes the value
	private taken(stored: string, value: Value): Value | undefined {
		if (!this.schema.known(stored)) return undefined;
		try {
			return this.schema.parse(unstore(stored)[0], value);
		} catch {
			return undefined;
		}
	}

	// another configuration over the same site and factory layers, what load
	// drops left out
	reader(values: Record<string, Value>): Values {
		const config = new Values(this.schema, this.site, this.factory);
		config.load(values);
		return config;
	}

	clone(): Values {
		return new Values(this.schema, this.site, this.factory, this.map);
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
