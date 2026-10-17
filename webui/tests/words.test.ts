import { describe, expect, it, vi } from 'vitest';
import { hints, word } from '../src/lib/words.js';
import { app, send } from '../src/lib/state.svelte.js';
import { rendering } from '../src/rendering/rendering.js';
import { settings } from '../src/config.js';
import { attached } from '../src/lib/sync.js';

// the browser database stays out: the conversations live in the page alone
vi.mock('../src/lib/db.js', () => ({
	listConversations: async () => [],
	putConversations: async () => {},
	deleteConversations: async () => {}
}));

describe('a word of the page', () => {
	it('is named by any prefix of one word alone, the rest of the line its own', () => {
		expect(word('ren set display thinking open')).toEqual({
			word: 'rendering',
			rest: 'set display thinking open'
		});
		expect(word('att')).toEqual({ word: 'attach', rest: '' });
		expect(word('de')).toEqual({ word: 'detach', rest: '' });
		expect(word('set chat model x')).toBeNull();
		expect(word('show running-config')).toBeNull();
		expect(word('')).toBeNull();
	});

	it('completes on the page, the rendering after its word', () => {
		expect(hints('re', {})).toEqual(['rendering']);
		expect(hints('rendering set display th', {})).toEqual(['thinking']);
		expect(hints('rendering set chat', {})).toEqual([]);
		expect(hints('set ', {})).toEqual([]);
	});

	it('runs on the page in no conversation, the rendering apart from the configuration', async () => {
		app.conversations = [];
		app.current = null;
		expect(await send('/rendering set display thinking open')).toBe(true);
		expect(app.said?.text).toBe('- set display thinking closed\n+ set display thinking open');
		expect(rendering.settings.get('display thinking')).toBe('open');
		expect(app.conversations).toEqual([]);
		await send('/rendering write');
		expect((await rendering.run('show startup-config', 'user')).text).toBe(
			'set display thinking open'
		);
		expect(localStorage.getItem('kiss.startup')).toBeNull();
		expect(await send('/attach')).toBe(false);
		expect(app.said?.text).toBe('% no mcp url backend, the KiSS server the page attaches to');
		expect(settings.get('mcp url', 'backend')).toBeUndefined();
		expect(attached()).toBe(false);
		expect(await send('/detach now')).toBe(false);
		expect(app.said?.text).toBe('% detach takes nothing after it');
		await send('/show running-config');
		expect(app.said).toBeNull();
		expect(app.conversations).toHaveLength(1);
	});
});
