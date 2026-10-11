import type { Archive, Save, Value } from '../lib/types.js';
import { ARCHIVE_STORAGE_KEY, STARTUP_STORAGE_KEY } from '../lib/config.js';

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

// the saves the browser keeps, oldest first
function saved(): Save[] {
	const saves = read(ARCHIVE_STORAGE_KEY);
	return Array.isArray(saves) ? saves : [];
}

// the saves, each under its own name, and the startup-config, what the page
// starts with, each kept in the browser on its own; a new state replaces the
// one in memory once the browser stores it
export class SaveArchive implements Archive {
	private saves = saved();
	private start = (read(STARTUP_STORAGE_KEY) as Record<string, Value> | null) ?? undefined;

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
		write(STARTUP_STORAGE_KEY, values);
		this.start = values;
	}

	private keep(saves: Save[]): void {
		write(ARCHIVE_STORAGE_KEY, saves);
		this.saves = saves;
	}
}
