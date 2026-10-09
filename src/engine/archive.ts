import type { Archive, Config, Module, Save } from '../lib/types.js';
import { ARCHIVE_STORAGE_KEY } from '../lib/config.js';

function read(): Save[] {
	try {
		const parsed = JSON.parse(localStorage.getItem(ARCHIVE_STORAGE_KEY) ?? '[]');
		return Array.isArray(parsed) ? parsed : [];
	} catch {
		return [];
	}
}

// the saves, each under its own name, oldest first, kept in the browser, the
// latest one starting the page; a new state replaces the one in memory once
// the browser stores it
export class SaveArchive implements Archive {
	private saves = read();

	constructor(private modules: readonly Module[]) {}

	private write(saves: Save[]): void {
		try {
			localStorage.setItem(ARCHIVE_STORAGE_KEY, JSON.stringify(saves));
		} catch {
			throw new Error('the browser refuses to store the archive');
		}
		this.saves = saves;
	}

	list(): readonly Save[] {
		return this.saves;
	}

	find(name: string): Save {
		const save = this.saves.find((s) => s.name === name);
		if (!save) throw new Error(`unknown save "${name}"`);
		return save;
	}

	latest(): Save | undefined {
		return this.saves.at(-1);
	}

	async save(config: Config, name: string): Promise<string[]> {
		const reasons = await Promise.all(
			this.modules.map((m) => (m.check ? m.check(config).catch((e: Error) => e.message) : null))
		);
		const save: Save = { name, date: new Date().toISOString(), values: config.values() };
		this.write([...this.saves.filter((s) => s.name !== name), save]);
		return reasons.filter((r) => r !== null);
	}

	remove(save: Save): void {
		this.write(this.saves.filter((s) => s !== save));
	}
}
