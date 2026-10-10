import { describe, expect, it, vi } from 'vitest';
import { compile } from 'svelte/compiler';
import { scope } from '../src/lib/config.js';
import css from '../src/modules/css.js';
import { app, send, stop } from '../src/lib/state.svelte.js';
import type { Config } from '../src/lib/types.js';

// the browser database stays out: the conversations live in the page alone
vi.mock('../src/lib/db.js', () => ({
	listConversations: async () => [],
	putConversations: async () => {},
	deleteConversations: async () => {}
}));

// every component of the page, its source by its file
const sources = import.meta.glob<string>('../src/**/*.svelte', {
	query: '?raw',
	import: 'default',
	eager: true
});
const name = (file: string): string => file.replace(/^.*\/|\.svelte$/g, '');

describe('style', () => {
	it('scopes the rules of a component by its name, which a css sheet aims at', () => {
		const file = '../src/components/Composer.svelte';
		const { css } = compile(sources[file], { filename: file, cssHash: scope });
		expect(css!.code).toMatch(/form\.svelte-composer\s*\{/);
		expect(css!.code).not.toMatch(/svelte-(?!composer\b)[a-z0-9]+/);
	});

	it('names every component once, so no two share a class', () => {
		const names = Object.keys(sources).map((f) => name(f).toLowerCase());
		expect(names.length).toBeGreaterThan(1);
		expect(new Set(names).size).toBe(names.length);
	});

	it('holds every token and css sheet off while a question stands, whatever it sets meanwhile', async () => {
		const media = () => [...document.head.querySelectorAll('style')].slice(-2).map((s) => s.media);
		const config = { names: () => ['cover'], get: () => '.svelte-ask { display: none }' };
		css.apply(config as unknown as Config);
		expect(media()).toEqual(['', '']);
		const sent = send('/import');
		await vi.waitFor(() => expect(app.asks[0]?.kind).toBe('pick'));
		expect(media()).toEqual(['not all', 'not all']);
		css.apply(config as unknown as Config);
		expect(media()).toEqual(['not all', 'not all']);
		stop(app.current!.id);
		await sent;
		expect(app.asks).toEqual([]);
		expect(media()).toEqual(['', '']);
	});

	it('reads every size of text floored, so no size hides what the thread shows', () => {
		const read = Object.values(sources).filter((s) => /var\(--size-/.test(s));
		expect(read).toEqual([]);
	});
});
