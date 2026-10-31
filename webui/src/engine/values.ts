import type { Config, Held, Schema, Value } from './engine.js';
import { REMOVED } from './text.js';
import { stored, unstore } from './schema.js';

// a configuration: the keys set in the session, over the values the site
// gives, over the factory configuration, over the reset value of each key; a
// layer takes a key back with null, the reset value applying from it down;
// the site and the factory layers are shared by every copy
export class Values implements Config {
	private map: Map<string, Held>;

	constructor(
		private schema: Schema,
		private site: Map<string, Held>,
		private factory: Map<string, Held>,
		map?: Map<string, Held>
	) {
		this.map = new Map(map);
	}

	get(key: string, name?: string): Value | undefined {
		const value = this.reveal(key, name);
		return value !== undefined && this.schema.find(key)?.kind === 'secret' ? REMOVED : value;
	}

	reveal(key: string, name?: string): Value | undefined {
		return this.given(stored(key, name)) ?? this.schema.find(key)?.default;
	}

	given(k: string): Held | undefined {
		if (this.map.has(k)) return this.map.get(k);
		return this.below(k);
	}

	// what the site, then the factory configuration, holds for a stored key
	private below(k: string): Held | undefined {
		if (this.site.has(k)) return this.site.get(k);
		return this.factory.get(k);
	}

	// an item exists while one of its keys has a value through the layers
	names(module: string): string[] {
		const names = [...this.stored(), ...this.factory.keys()]
			.filter((k) => this.given(k) != null)
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

	takeBack(key: string, name?: string): void {
		const k = stored(key, name);
		if (this.map.get(k) == null && this.below(k) != null) this.map.set(k, null);
		else this.map.delete(k);
	}

	drop(module: string, name: string): void {
		for (const k of new Set([...this.stored(), ...this.factory.keys()])) {
			const [key, item] = unstore(k);
			if (item === name && key.startsWith(module + ' ')) this.takeBack(key, item);
		}
	}

	stored(): string[] {
		return [...new Set([...this.map.keys(), ...this.site.keys()])].sort();
	}

	values(): Record<string, Held> {
		return Object.fromEntries([...this.map].sort(([a], [b]) => a.localeCompare(b)));
	}

	load(values: Record<string, Held>): string[] {
		const kept: [string, Held][] = [];
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
	// key anymore, the key no longer takes the value, or nothing below holds
	// the value it takes back
	private taken(stored: string, value: Held): Held | undefined {
		if (!this.schema.known(stored)) return undefined;
		if (value === null) return this.below(stored) != null ? null : undefined;
		try {
			return this.schema.parse(unstore(stored)[0], value);
		} catch {
			return undefined;
		}
	}

	// another configuration over the same site and factory layers, what load
	// drops left out
	reader(values: Record<string, Held>): Values {
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
