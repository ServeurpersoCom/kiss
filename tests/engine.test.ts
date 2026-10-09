import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Grant, Verdict } from '../src/lib/types.js';
import { ALWAYS, ONCE, REFUSE } from '../src/lib/types.js';

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

// the user lets every change the model asks for, once
const grant = async () => ONCE;

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
		await expect(k.run('set chat system blue', 'llm', { signal: stop.signal })).rejects.toThrow();
		expect(k.settings.get('chat system')).toBe('');
	});

	it('a waiting batch replaces nothing', async () => {
		const k = await page();
		const h = held();
		vi.stubGlobal('fetch', h.fetch);
		await k.run('set endpoints url a http://a/v1', 'user');
		const stop = new AbortController();
		const r = k.run('set chat system blue\nshow models', 'llm', { signal: stop.signal });
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
		const queued = k.run('set chat system blue', 'llm', { signal: stop.signal });
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
		expect((await k.run('set endpoints url fresh http://f/v1', 'llm', { grant })).ok).toBe(true);
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
		expect(k.settings.get('tools use', 'repo.search')).toBe('consent');
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
		expect(k.settings.get('tools use', 'repo.search')).toBe('consent');
	});
});

describe('a title', () => {
	it('renames the conversation of the batch with the batch, and no save keeps it', async () => {
		const k = await page();
		const c = { title: 'hello' };
		expect((await k.run('title "Bonjour le monde"', 'llm', { conversation: c })).text).toBe(
			'- title hello\n+ title "Bonjour le monde"'
		);
		expect(c.title).toBe('Bonjour le monde');
		expect((await k.run('show title', 'user', { conversation: c })).text).toBe(
			'title "Bonjour le monde"'
		);
		const failed = await k.run('title other\nset display tools wide', 'llm', { conversation: c });
		expect(failed.ok).toBe(false);
		expect(c.title).toBe('Bonjour le monde');
		const long = await k.run(`title ${'a'.repeat(61)}`, 'llm', { conversation: c });
		expect(long.text).toContain('60 characters at most');
		expect((await k.run('title "a\\nb"', 'llm', { conversation: c })).text).toContain('one line');
		expect((await k.run('title ""', 'llm', { conversation: c })).text).toContain('a word at least');
		expect((await k.run('title x', 'llm')).text).toContain('no conversation here');
		await k.run('save a', 'user');
		expect(localStorage.getItem('kiss.saves')).not.toContain('Bonjour');
	});
});

describe('a word after set, no or show', () => {
	it('competes with the modules, a prefix of both being ambiguous', async () => {
		const k = await page();
		expect((await k.run('show d', 'user')).text).toBe('% ambiguous "d": diff display');
		expect((await k.run('show t', 'user')).text).toBe('% ambiguous "t": title tools');
		expect((await k.run('show dis', 'user')).text).toContain('set display thinking closed');
		expect((await k.run('show tool', 'user')).ok).toBe(true);
		expect((await k.run('show ti', 'user')).text).toContain('no conversation here');
	});
});

// a user answering every change the model asks for with one verdict, the
// questions kept
function user(verdict: Verdict) {
	const asked: Grant[] = [];
	return { asked, grant: async (request: Grant) => (asked.push(request), verdict) };
}

describe('the firewall', () => {
	it('asks the user before the model changes a guarded key, whatever spells it', async () => {
		const k = await page();
		await k.run('set tools use t off\nset chat system mine', 'user');
		const once = user(ONCE);
		expect((await k.run('set chat system theirs', 'llm', once)).ok).toBe(true);
		expect(once.asked).toEqual([
			{ kind: 'change', lines: ['- set chat system mine', '+ set chat system theirs'] }
		]);
		const refuse = user(REFUSE);
		expect((await k.run('reset', 'llm', refuse)).text).toBe('% the user refused the change');
		expect((await k.run('no tools t', 'llm', refuse)).text).toBe('% the user refused the change');
		expect(refuse.asked.map((a) => a.kind === 'change' && a.lines)).toEqual([
			[
				'- set chat system theirs',
				'+ set chat system ""',
				'- set tools use t off',
				'+ set tools use t consent'
			],
			['- set tools use t off', '+ set tools use t consent']
		]);
		expect(k.settings.get('tools use', 't')).toBe('off');
	});

	it('never asks when the model closes', async () => {
		const k = await page();
		const refuse = user(REFUSE);
		expect((await k.run('set tools use t off', 'llm', refuse)).ok).toBe(true);
		expect(refuse.asked).toEqual([]);
		expect((await k.run('set tools use t consent', 'llm', refuse)).ok).toBe(false);
	});

	it('goes as far as the privilege of the module, which only the user sets', async () => {
		const k = await page();
		const once = user(ONCE);
		await k.run('set privilege level chat deny', 'user');
		expect((await k.run('set chat system x', 'llm', once)).text).toBe(
			'% the privilege of chat denies the change'
		);
		await k.run('set privilege level chat allow', 'user');
		expect((await k.run('set chat system x', 'llm', once)).ok).toBe(true);
		expect(once.asked).toEqual([]);
		expect((await k.run('set privilege level chat ask', 'llm', once)).text).toBe(
			"% privilege is the user's"
		);
		expect((await k.run('reset', 'llm', once)).text).toBe("% privilege is the user's");
		expect((await k.run('set privilege level css allow', 'user')).text).toContain(
			'css guards no key'
		);
		expect((await k.run('show privilege', 'user')).text).toBe(
			[
				'! modules',
				'set privilege level chat allow',
				'set privilege level endpoints ask',
				'set privilege level mcp ask',
				'set privilege level tools ask'
			].join('\n')
		);
	});

	it('takes always as the allow privilege of the modules asked', async () => {
		const k = await page();
		expect((await k.run('set chat model a/b', 'llm', user(ALWAYS))).text).toBe(
			[
				'- set chat model ""',
				'+ set chat model a/b',
				'- set privilege level chat ask',
				'+ set privilege level chat allow'
			].join('\n')
		);
		expect((await k.run('set chat system y', 'llm', user(REFUSE))).ok).toBe(true);
	});

	it('refuses when nobody is here, and a stop while it asks applies nothing', async () => {
		const k = await page();
		expect((await k.run('set chat system x', 'llm')).text).toBe(
			'% nobody is here to agree to the change'
		);
		const stop = new AbortController();
		let answer: ((v: Verdict) => void) | undefined;
		const grant = () => new Promise<Verdict>((resolve) => (answer = resolve));
		const r = k.run('set chat system x', 'llm', { signal: stop.signal, grant });
		await vi.waitFor(() => expect(answer).toBeDefined());
		stop.abort();
		answer!(ONCE);
		await expect(r).rejects.toThrow();
		expect(k.settings.get('chat system')).toBe('');
	});

	it('asks the value of a secret left out, which never shows', async () => {
		const k = await page();
		await k.run('set mcp url a http://a/mcp', 'user');
		const asked: string[] = [];
		const secret = async (key: string) => (asked.push(key), 'sk-typed');
		expect((await k.run('set mcp key a', 'llm', { secret })).text).toBe('+ ! mcp key a is set');
		expect(asked).toEqual(['mcp key a']);
		expect(k.settings.get('mcp key', 'a')).toBe('sk-typed');
		expect((await k.run('set mcp key a', 'llm', { secret: async () => null })).text).toBe(
			'% no value given for mcp key a'
		);
		expect((await k.run('set mcp key a', 'llm')).text).toBe('% nobody is here to give mcp key a');
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
