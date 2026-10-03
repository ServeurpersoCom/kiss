import { mount } from 'svelte';
import App from './App.svelte';
import { defaults, run, start } from './engine/run.js';
import { restore } from './lib/state.svelte.js';
import { SITE_CONFIG_URL } from './lib/config.js';

// a missing site configuration is the common case; a static server may answer
// a missing file with its index page, which is not a configuration either
const site = await fetch(SITE_CONFIG_URL).catch(() => null);
if (site?.ok && !site.headers.get('content-type')?.includes('html')) {
	for (const problem of defaults(await site.text()))
		console.error(`${SITE_CONFIG_URL}: ${problem}`);
}

start();
await restore();

// development only: run CLI lines from the browser console, kiss('show running', 'llm')
if (import.meta.env.DEV) Object.assign(globalThis, { kiss: run });

mount(App, { target: document.getElementById('app')! });
