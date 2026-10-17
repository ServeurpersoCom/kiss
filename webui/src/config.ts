import type { Command, Module } from './engine/engine.js';
import { engine } from './engine/run.js';
import { registry } from './engine/registry.js';
import { SaveArchive } from './engine/archive.js';
import { hold } from './engine/keychain.js';
import {
	ARCHIVE_STORAGE_KEY,
	FACTORY_FILE,
	SITE_CONFIG_URL,
	STARTUP_STORAGE_KEY
} from './lib/config.js';
import factory from '../client.conf?raw';

// the configuration of the page, twin of the configuration of the server: a
// file in commands/ or modules/ registers itself by existing; the keychain
// reads its secrets
export const config = engine({
	registry: registry(
		import.meta.glob<{ default: Command<unknown> }>('./commands/*.ts', { eager: true }),
		import.meta.glob<{ default: Module }>('./modules/*.ts', { eager: true })
	),
	factory,
	factoryFile: FACTORY_FILE,
	siteFile: SITE_CONFIG_URL,
	archive: new SaveArchive(ARCHIVE_STORAGE_KEY, STARTUP_STORAGE_KEY)
});

hold(config.secrets);

export const { settings, modules, resolve, defaults, start, run, redact, suggest, known } = config;
