import { mount } from 'svelte';
import './rendering/tokens.css';
import App from './rendering/App.svelte';
import { config, run } from './config.js';
import { rendering } from './rendering/rendering.js';
import { restore, unlock } from './lib/state.svelte.js';
import { RENDERING_SITE_URL, SITE_CONFIG_URL } from './lib/config.js';

// the text of a site file next to the page, none when it is missing, the
// common case: a static server may answer a missing file with its index page,
// which is not a configuration either
async function sited(url: string): Promise<string | null> {
	const res = await fetch(url).catch(() => null);
	return res?.ok && !res.headers.get('content-type')?.includes('html') ? res.text() : null;
}

// each configuration of the page boots over its site file: the configuration,
// twin of the server, then the rendering
for (const [name, c, url] of [
	['startup-config', config, SITE_CONFIG_URL],
	['rendering startup-config', rendering, RENDERING_SITE_URL]
] as const) {
	const text = await sited(url);
	if (text !== null) for (const problem of c.defaults(text)) console.error(`${url}: ${problem}`);
	for (const problem of c.start()) console.error(`${name}: ${problem}`);
}
await restore();

// development only: run CLI lines from the browser console, kiss('show running', 'llm')
if (import.meta.env.DEV) Object.assign(globalThis, { kiss: run });

mount(App, { target: document.getElementById('app')! });

// a server that takes no request without its key asks the user for it as the
// page opens
void unlock();
