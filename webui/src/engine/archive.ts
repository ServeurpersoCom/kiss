import type { Archive, Save, Value } from './engine.js';

// what the browser keeps under a key, none when it keeps nothing readable
function read(key: string): unknown {
	try {
		return JSON.parse(localStorage.getItem(key) ?? 'null');
	} catch {
		return null;
	}
}

// what the browser keeps under a key, none removing it
function write(key: string, value: unknown): void {
	try {
		if (value === undefined) localStorage.removeItem(key);
		else localStorage.setItem(key, JSON.stringify(value));
	} catch {
		throw new Error('the browser refuses to store the archive');
	}
}

// the saves the browser keeps under a key, oldest first
function saved(key: string): Save[] {
	const saves = read(key);
	return Array.isArray(saves) ? saves : [];
}

// the saves, each under its own name, and the startup-config, what the page
// starts with, each kept in the browser on its own under the keys given; a
// new state replaces the one in memory once the browser stores it
export class SaveArchive implements Archive {
	private saves: Save[];
	private start: Record<string, Value> | undefined;

	constructor(
		private savesKey: string,
		private startupKey: string
	) {
		this.saves = saved(savesKey);
		this.start = (read(startupKey) as Record<string, Value> | null) ?? undefined;
	}

	list(): readonly Save[] {
		return this.saves;
	}

	find(name: string): Save {
		const save = this.saves.find((s) => s.name === name);
		if (!save) throw new Error(`unknown save "${name}"`);
		return save;
	}

	save(name: string, values: Record<string, Value>): void {
		const save: Save = { name, date: new Date().toISOString(), values };
		this.keep([...this.saves.filter((s) => s.name !== name), save]);
	}

	remove(save: Save): void {
		this.keep(this.saves.filter((s) => s !== save));
	}

	startup(): Record<string, Value> {
		return this.start ?? {};
	}

	boot(values: Record<string, Value> | undefined): void {
		write(this.startupKey, values);
		this.start = values;
	}

	private keep(saves: Save[]): void {
		write(this.savesKey, saves);
		this.saves = saves;
	}
}
