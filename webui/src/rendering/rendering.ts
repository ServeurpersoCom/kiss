import type { Command, Module } from '../engine/engine.js';
import { engine } from '../engine/run.js';
import { registry } from '../engine/registry.js';
import { SaveArchive } from '../engine/archive.js';
import {
	RENDERING_ARCHIVE_STORAGE_KEY,
	RENDERING_FACTORY_FILE,
	RENDERING_SITE_URL,
	RENDERING_STARTUP_STORAGE_KEY
} from '../lib/config.js';
import factory from '../../rendering.conf?raw';

// the rendering of the page, a configuration of its own that no other machine
// has: how the thread shows a turn and the style of the page, set, saved and
// copied as IOS does, by the commands of every configuration and its own
export const rendering = engine({
	registry: registry(
		{
			...import.meta.glob<{ default: Command<unknown> }>(
				'../commands/{set,no,show,show-running,show-startup,show-saves,show-diff,copy,write,erase}.ts',
				{ eager: true }
			),
			...import.meta.glob<{ default: Command<unknown> }>('./commands/*.ts', { eager: true })
		},
		import.meta.glob<{ default: Module }>('./modules/*.ts', { eager: true })
	),
	factory,
	factoryFile: RENDERING_FACTORY_FILE,
	siteFile: RENDERING_SITE_URL,
	archive: new SaveArchive(RENDERING_ARCHIVE_STORAGE_KEY, RENDERING_STARTUP_STORAGE_KEY)
});
