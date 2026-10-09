import { beforeEach, describe, expect, it, vi } from 'vitest';

type Engine = typeof import('../src/engine/run.js');

// a page load: the engine starts over what the browser keeps, below a site
// configuration
async function page(site = ''): Promise<Engine> {
	vi.resetModules();
	const engine = await import('../src/engine/run.js');
	engine.defaults(site);
	engine.start();
	return engine;
}

function models(ids: string[]): Response {
	return new Response(JSON.stringify({ data: ids.map((id) => ({ id })) }), {
		headers: { 'content-type': 'application/json' }
	});
}

// a model list held until released, aborted with the signal of its request
function held() {
	let release = () => {};
	const fetch = vi.fn(
		(_url: string, init?: RequestInit) =>
			new Promise<Response>((resolve, reject) => {
				release = () => resolve(models(['m']));
				init?.signal?.addEventListener('abort', () => reject(init.signal!.reason));
			})
	);
	return { fetch, release: () => release() };
}

beforeEach(() => {
	localStorage.clear();
	vi.unstubAllGlobals();
	vi.doUnmock('../src/lib/config.js');
});

describe('a batch', () => {
	it('applies whole or not at all', async () => {
		const k = await page();
		const r = await k.run('set display tools open\nset display tools wide', 'user');
		expect(r.ok).toBe(false);
		expect(r.text).toContain('nothing applied');
		expect(k.settings.get('display tools')).toBe('closed');
	});

	it('compiles every line before the first one runs', async () => {
		const k = await page();
		const fetch = vi.fn();
		vi.stubGlobal('fetch', fetch);
		await k.run('set endpoints url a http://a/v1', 'user');
		const r = await k.run('show models\nshow running | bogus', 'user');
		expect(r.text).toContain('line 2: unknown filter');
		expect(fetch).not.toHaveBeenCalled();
		expect((await k.run('save a | bogus', 'user')).ok).toBe(false);
		expect((await k.run('show saves', 'user')).text).toBe('! nothing saved yet');
	});

	it('runs a command that writes the archive alone', async () => {
		const k = await page();
		const r = await k.run('set display tools open\nsave a', 'user');
		expect(r.text).toContain('save runs alone');
		expect((await k.run('show saves', 'user')).text).toBe('! nothing saved yet');
	});

	it('takes no word its command does not read', async () => {
		const k = await page();
		expect((await k.run('show running now', 'user')).text).toContain('nothing goes after');
		expect((await k.run('show chat system now', 'user')).text).toContain('nothing goes after');
	});

	it('answers with the change of the resolved values, secrets masked', async () => {
		const k = await page('set chat system red');
		expect((await k.run('set chat system blue', 'user')).text).toBe(
			'- set chat system red\n+ set chat system blue'
		);
		expect((await k.run('no chat system', 'user')).text).toBe(
			'- set chat system blue\n+ set chat system red'
		);
		const key = await k.run('set endpoints url a http://a/v1\nset endpoints key a sk-123', 'user');
		expect(key.text).toBe('+ ! endpoints key a is set\n+ set endpoints url a http://a/v1');
	});

	it('runs one at a time, whoever sends it', async () => {
		const k = await page();
		const h = held();
		vi.stubGlobal('fetch', h.fetch);
		await k.run('set endpoints url a http://a/v1', 'user');
		const slow = k.run('show models', 'llm');
		const set = k.run('set chat system blue', 'user');
		await vi.waitFor(() => expect(h.fetch).toHaveBeenCalled());
		h.release();
		expect((await slow).text).toBe('! endpoints a\n! a/m');
		await set;
		expect(k.settings.get('chat system')).toBe('blue');
	});
});

describe('a model list', () => {
	it('groups every endpoint at once, in order, a failed one beside the others', async () => {
		const k = await page();
		let release = () => {};
		const gate = new Promise<void>((resolve) => (release = resolve));
		const fetch = vi.fn(async (url: string) => {
			if (url.startsWith('http://a/')) throw new Error('down');
			await gate;
			return models(url.startsWith('http://b/') ? ['m', 'org/n'] : []);
		});
		vi.stubGlobal('fetch', fetch);
		await k.run('set endpoints url b http://b/v1\nset endpoints url a http://a/v1', 'user');
		await k.run('set endpoints url c http://c/v1', 'user');
		const r = k.run('show models', 'user');
		await vi.waitFor(() => expect(fetch).toHaveBeenCalledTimes(3));
		release();
		expect((await r).text).toBe('! endpoints a down\n! endpoints b\n! b/m\n! b/org/n');
	});

	it('that never answers frees the queue after its timeout', async () => {
		const k = await page();
		vi.stubGlobal('fetch', held().fetch);
		await k.run('set endpoints url a http://a/v1\nset endpoints timeout a 0.1', 'user');
		expect((await k.run('show models', 'user')).text).toBe(
			'! endpoints a a does not answer within 0.1 s'
		);
		expect((await k.run('set chat system blue', 'user')).ok).toBe(true);
	});

	it('whose body never ends frees the queue after its timeout', async () => {
		const k = await page();
		const fetch = vi.fn(async (_url: string, init?: RequestInit) => {
			const body = new ReadableStream({
				start(c) {
					init?.signal?.addEventListener('abort', () => c.error(init.signal!.reason));
				}
			});
			return new Response(body, { headers: { 'content-type': 'application/json' } });
		});
		vi.stubGlobal('fetch', fetch);
		await k.run('set endpoints url a http://a/v1\nset endpoints timeout a 0.1', 'user');
		expect((await k.run('show models', 'user')).text).toContain('a does not answer within 0.1 s');
		expect((await k.run('set chat system blue', 'user')).ok).toBe(true);
	});
});

describe('after a stop', () => {
	it('a batch never starts', async () => {
		const k = await page();
		const stop = new AbortController();
		stop.abort();
		await expect(k.run('set chat system blue', 'llm', stop.signal)).rejects.toThrow();
		expect(k.settings.get('chat system')).toBe('');
	});

	it('a waiting batch replaces nothing', async () => {
		const k = await page();
		const h = held();
		vi.stubGlobal('fetch', h.fetch);
		await k.run('set endpoints url a http://a/v1', 'user');
		const stop = new AbortController();
		const r = k.run('set chat system blue\nshow models', 'llm', stop.signal);
		await vi.waitFor(() => expect(h.fetch).toHaveBeenCalled());
		stop.abort();
		await expect(r).rejects.toThrow();
		expect(k.settings.get('chat system')).toBe('');
	});

	it('a queued batch never starts', async () => {
		const k = await page();
		const h = held();
		vi.stubGlobal('fetch', h.fetch);
		await k.run('set endpoints url a http://a/v1', 'user');
		const first = k.run('show models', 'user');
		const stop = new AbortController();
		const queued = k.run('set chat system blue', 'llm', stop.signal);
		await vi.waitFor(() => expect(h.fetch).toHaveBeenCalled());
		stop.abort();
		h.release();
		await first;
		await expect(queued).rejects.toThrow();
		expect(k.settings.get('chat system')).toBe('');
	});
});

describe('a secret', () => {
	it('only ever goes to a URL the user or the site chose', async () => {
		const k = await page('set endpoints key s sk-site\nset endpoints url s http://s/v1');
		await k.run('set endpoints url u http://u/v1\nset endpoints key u sk-user', 'user');
		for (const item of ['s', 'u']) {
			const r = await k.run(`set endpoints url ${item} http://evil/v1`, 'llm');
			expect(r.text).toContain(`endpoints ${item} holds a secret`);
		}
		expect((await k.run('set endpoints key u sk-model', 'llm')).ok).toBe(true);
		expect((await k.run('set endpoints url fresh http://f/v1', 'llm')).ok).toBe(true);
		expect((await k.run('set endpoints url u http://u2/v1', 'user')).ok).toBe(true);
	});

	it('never prints', async () => {
		const k = await page();
		await k.run('set endpoints url u http://u/v1\nset endpoints key u sk-user', 'user');
		const shown = (await k.run('show running', 'user')).text;
		expect(shown).toContain('! endpoints key u is set');
		expect(shown).not.toContain('sk-user');
		expect(k.redact('set endpoints key u sk-user')).toBe('set endpoints key u ****');
		expect(k.redact('set endpoints key u "sk-user')).toBe('set endpoints key u ****');
	});
});

describe('show running', () => {
	it('reads back to the same values', async () => {
		const k = await page();
		const lines = [
			'set css sheet a ":root { --accent: oklch(0.6 0.2 300) }"',
			'set chat system "a|b \\"c\\"\\nd"',
			'set css sheet b \'body { font-family: "Inter", sans-serif }\'',
			'set display tools open',
			'set endpoints url a http://a/v1'
		];
		expect((await k.run(lines.join('\n'), 'user')).ok).toBe(true);
		expect(k.settings.get('chat system')).toBe('a|b "c"\nd');
		expect(k.settings.get('display tools')).toBe('open');
		const shown = (await k.run('show running', 'user')).text;
		const fresh = await page();
		const sets = shown
			.split('\n')
			.filter((l) => !l.startsWith('!'))
			.join('\n');
		expect((await fresh.run(sets, 'user')).ok).toBe(true);
		expect((await fresh.run('show running', 'user')).text).toBe(shown);
	});
});

describe('the display', () => {
	it('folds by default and opens on demand', async () => {
		const k = await page();
		expect(k.settings.get('display thinking')).toBe('closed');
		expect(k.settings.get('display tools')).toBe('closed');
		expect((await k.run('set display tools ajar', 'user')).text).toContain(
			'not one of closed open'
		);
		expect((await k.run('set display tools open', 'user')).ok).toBe(true);
	});
});

describe('a collection', () => {
	it('is named in the plural, its singular naming it too', async () => {
		const k = await page();
		await k.run('set endpoint url a http://a/v1', 'user');
		expect((await k.run('show endpoint url', 'user')).text).toBe('set endpoints url a http://a/v1');
		expect((await k.run('show save', 'user')).text).toBe('! nothing saved yet');
	});
});

describe('a tool', () => {
	it('holds every setting of its own under its name, a dotted one too', async () => {
		const k = await page();
		expect(k.settings.get('tools use', 'repo.search')).toBe('on');
		const r = await k.run(
			'set tools preview repo.search query\nset tools use repo.search off',
			'user'
		);
		expect(r.ok).toBe(true);
		expect((await k.run('show tools repo.search', 'user')).text).toBe(
			'set tools preview repo.search query\nset tools use repo.search off'
		);
		expect((await k.run('no tools repo.search', 'user')).ok).toBe(true);
		expect(k.settings.get('tools preview', 'repo.search')).toBeUndefined();
		expect(k.settings.get('tools use', 'repo.search')).toBe('on');
	});
});

describe('kiss.conf', () => {
	it('applies whole or not at all, the rules of every module holding', async () => {
		vi.resetModules();
		const k = await import('../src/engine/run.js');
		expect(k.defaults('set chat system red\nset endpoints key s sk-site')).toEqual([
			'endpoints: endpoints s has no url'
		]);
		expect(k.defaults('set chat system red\nshow running')).toEqual([
			'show running: only set lines'
		]);
		k.start();
		expect(k.settings.get('chat system')).toBe('');
		expect((await k.run('set chat system blue', 'user')).ok).toBe(true);
	});
});

describe('the archive', () => {
	it('stays as it was when the browser refuses to store it', async () => {
		const k = await page();
		await k.run('save one', 'user');
		vi.stubGlobal('localStorage', {
			setItem() {
				throw new Error('quota');
			}
		});
		expect((await k.run('save two', 'user')).text).toContain('refuses to store');
		vi.unstubAllGlobals();
		expect((await k.run('show saves', 'user')).text).not.toContain('two');
		expect((await k.run('save three', 'user')).text).toBe('saved three');
	});
});

describe('a model', () => {
	it('takes the name a server gives it, and numbers within their bounds', async () => {
		const k = await page();
		expect((await k.run('set models temperature hf/org/m:tag 0.70', 'user')).ok).toBe(true);
		expect(k.settings.get('models temperature', 'hf/org/m:tag')).toBe('0.7');
		expect((await k.run('set models top_p a/b 2', 'user')).text).toContain('2 is above 1');
		expect((await k.run('set models top_k a/b 1.5', 'user')).text).toContain('not a whole number');
	});
});

describe('a save', () => {
	it('warns when no model answers the chat', async () => {
		const k = await page();
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => models(['m']))
		);
		await k.run('set endpoints url a http://a/v1\nset endpoints url b http://b/v1', 'user');
		expect((await k.run('save a', 'user')).text).toBe(
			'saved a\n! chat model is not set: /set chat model <endpoint/model>, see /show models'
		);
		await k.run('set chat model b/x', 'user');
		expect((await k.run('save a', 'user')).text).toBe(
			'saved a\n! b does not serve x, see show models'
		);
		await k.run('set chat model b/m', 'user');
		expect((await k.run('save a', 'user')).text).toBe('saved a');
	});

	it('takes one name, never session, and a save of the same name gives way', async () => {
		const k = await page();
		expect((await k.run('save', 'user')).text).toBe('<name>');
		expect((await k.run('save session', 'user')).text).toContain('is not a save name');
		expect((await k.run('save a b', 'user')).text).toContain('nothing goes after');
		vi.useFakeTimers({ now: new Date('2026-10-05T09:17:00Z'), toFake: ['Date'] });
		await k.run('set chat system blue', 'user');
		await k.run('save a', 'user');
		vi.setSystemTime(new Date('2026-10-05T09:20:00Z'));
		await k.run('save b', 'user');
		vi.setSystemTime(new Date('2026-10-05T09:25:00Z'));
		await k.run('set chat system green', 'user');
		await k.run('save a', 'user');
		vi.useRealTimers();
		expect((await k.run('show saves', 'user')).text).toBe('b 2026-10-05 11:20\na 2026-10-05 11:25');
		expect((await page()).settings.get('chat system')).toBe('green');
	});

	it('compares with another, or with the session', async () => {
		const k = await page();
		await k.run('set chat system blue', 'user');
		await k.run('save a', 'user');
		await k.run('set chat system green', 'user');
		expect((await k.run('show diff a session', 'user')).text).toBe(
			'- set chat system blue\n+ set chat system green'
		);
		expect((await k.run('show diff session session', 'user')).text).toBe('! no difference');
		expect((await k.run('show diff a', 'user')).text).toBe('a\nsession');
	});
});

describe('show', () => {
	it('reads a module four ways, key before item as everywhere', async () => {
		const k = await page();
		await k.run('set endpoints url a http://a/v1\nset endpoints url b http://b/v1', 'user');
		await k.run('set endpoints key a sk-a', 'user');
		expect((await k.run('show endpoints', 'user')).text).toBe(
			[
				'! endpoints key a is set',
				'set endpoints timeout a 10',
				'set endpoints url a http://a/v1',
				'set endpoints timeout b 10',
				'set endpoints url b http://b/v1'
			].join('\n')
		);
		expect((await k.run('show endpoints url', 'user')).text).toBe(
			'set endpoints url a http://a/v1\nset endpoints url b http://b/v1'
		);
		expect((await k.run('show endpoints url b', 'user')).text).toBe(
			'set endpoints url b http://b/v1'
		);
		expect((await k.run('show endpoints key b', 'user')).text).toBe('! endpoints key b is not set');
		expect((await k.run('show endpoints a', 'user')).text).toBe(
			'! endpoints key a is set\nset endpoints timeout a 10\nset endpoints url a http://a/v1'
		);
		expect((await k.run('show display zz', 'user')).text).toContain('unknown key');
	});
});

describe('css', () => {
	it('is the last style sheet of the page, its sheets by name, gone with a reset', async () => {
		const k = await page();
		await k.run(`set css sheet b 'b { color: red }'\nset css sheet a 'a { color: blue }'`, 'user');
		const last = () => document.head.querySelector('style:last-of-type')!.textContent;
		expect(last()).toBe('a { color: blue }\nb { color: red }');
		await k.run('reset', 'user');
		expect(last()).toBe('');
	});
});

describe('reset and load', () => {
	it('reset drops what the session sets, load puts back the save it names', async () => {
		const k = await page('set display tools open');
		expect((await k.run('load first', 'user')).text).toContain('no save first');
		await k.run('set chat system blue', 'user');
		await k.run('save first', 'user');
		await k.run('set chat system green', 'user');
		await k.run('save second', 'user');
		await k.run('set chat system pink\nset display tools closed', 'user');
		await k.run('reset', 'user');
		expect(k.settings.get('chat system')).toBe('');
		expect(k.settings.get('display tools')).toBe('open');
		await k.run('load second', 'user');
		expect(k.settings.get('chat system')).toBe('green');
		await k.run('load first', 'user');
		expect(k.settings.get('chat system')).toBe('blue');
	});
});

describe('a page', () => {
	it('saved right after a reset starts with the kiss.conf of the moment', async () => {
		const k = await page();
		await k.run('set chat system blue', 'user');
		await k.run('save mine', 'user');
		await k.run('reset', 'user');
		await k.run('save site', 'user');
		const next = await page('set display tools open');
		expect(next.settings.get('chat system')).toBe('');
		expect(next.settings.get('display tools')).toBe('open');
	});

	it('starts with the latest save, over the kiss.conf of the moment', async () => {
		const k = await page();
		await k.run('set chat system blue', 'user');
		await k.run('save a', 'user');
		await k.run('set chat system green', 'user');
		await k.run('save b', 'user');
		await k.run('set chat system pink', 'user');
		const next = await page('set display tools open');
		expect(next.settings.get('chat system')).toBe('green');
		expect(next.settings.get('display tools')).toBe('open');
	});
});
