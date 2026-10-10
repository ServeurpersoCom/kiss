import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Conversation, Grant, Library, Verdict } from '../src/lib/types.js';
import { append, fresh, named, parse, serialize } from '../src/lib/conversation.js';
import { localTime } from '../src/lib/config.js';
import { ALWAYS, ONCE, REFUSE, always, answers } from '../src/lib/types.js';

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
		const r = await k.run('show models\nshow running-config | bogus', 'user');
		expect(r.text).toContain('line 2: unknown filter');
		expect(fetch).not.toHaveBeenCalled();
		expect((await k.run('copy running-config a | bogus', 'user')).ok).toBe(false);
		expect((await k.run('show saves', 'user')).text).toBe('! nothing saved yet');
	});

	it('runs a command that writes the archive alone', async () => {
		const k = await page();
		const r = await k.run('set display tools open\ncopy running-config a', 'user');
		expect(r.text).toContain('copy runs alone');
		expect((await k.run('show saves', 'user')).text).toBe('! nothing saved yet');
	});

	it('takes no word its command does not read', async () => {
		const k = await page();
		expect((await k.run('show running-config now', 'user')).text).toBe(
			'% nothing goes after show running-config: now'
		);
		expect((await k.run('show chat system now', 'user')).text).toBe(
			'% nothing goes after show chat system: now'
		);
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
			'! endpoints a http://a/v1 does not answer within 0.1 s'
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
		expect((await k.run('show models', 'user')).text).toContain(
			'http://a/v1 does not answer within 0.1 s'
		);
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
		expect((await k.run('set endpoints timeout u 9', 'llm')).ok).toBe(true);
		expect((await k.run('set endpoints url fresh http://f/v1', 'llm', { grant })).ok).toBe(true);
		expect((await k.run('set endpoints url u http://u2/v1', 'user')).ok).toBe(true);
	});

	it('never prints', async () => {
		const k = await page();
		await k.run('set endpoints url u http://u/v1\nset endpoints key u sk-user', 'user');
		const shown = (await k.run('show running-config', 'user')).text;
		expect(shown).toContain('! endpoints key u is set');
		expect(shown).not.toContain('sk-user');
		expect(k.redact('set endpoints key u sk-user')).toBe('set endpoints key u <removed>');
		expect(k.redact('set endpoints key u "sk-user')).toBe('set endpoints key u <removed>');
	});
});

describe('a filter', () => {
	it('takes the rest of the line as its pattern, as IOS does, and the output comes whole', async () => {
		const k = await page();
		const urls = Array.from({ length: 250 }, (_, i) => `set endpoints url e${i} http://e/v1`);
		await k.run(urls.join('\n'), 'user');
		const all = (await k.run('show running-config', 'llm')).text.split('\n');
		expect(all).toHaveLength(250);
		const run = async (filter: string) =>
			(await k.run(`show running-config | ${filter}`, 'llm')).text;
		expect(await run('include url e(7|42) http')).toBe(
			'set endpoints url e42 http://e/v1\nset endpoints url e7 http://e/v1'
		);
		expect(await run('inc url e7 | x')).toBe('set endpoints url e7 http://e/v1');
		expect(await run('count')).toBe('250');
		expect(await run('begin url E99 ')).toBe('set endpoints url e99 http://e/v1');
		expect(await run('exclude url e[0-9]+ ')).toBe('! no line matches');
		expect(await run('include (')).toBe('% "(" is not a pattern');
		expect(await run('count x')).toBe('% count takes no pattern: x');
		expect(await run('sort')).toContain('% unknown filter "sort"');
	});
});

describe('show running-config', () => {
	it('reads back to the same values', async () => {
		const k = await page();
		const lines = [
			'set style sheet a ":root { --accent: oklch(0.6 0.2 300) }"',
			'set chat system "a|b \\"c\\"\\nd"',
			'set style sheet b \'body { font-family: "Inter", sans-serif }\'',
			'set display tools open',
			'set endpoints url a http://a/v1'
		];
		expect((await k.run(lines.join('\n'), 'user')).ok).toBe(true);
		expect(k.settings.get('chat system')).toBe('a|b "c"\nd');
		expect(k.settings.get('display tools')).toBe('open');
		const shown = (await k.run('show running-config', 'user')).text;
		const fresh = await page();
		const sets = shown
			.split('\n')
			.filter((l) => !l.startsWith('!'))
			.join('\n');
		expect((await fresh.run(sets, 'user')).ok).toBe(true);
		expect((await fresh.run('show running-config', 'user')).text).toBe(shown);
	});

	it('all lists every module as show lists it, defaults included', async () => {
		const k = await page();
		await k.run('set display tools open', 'user');
		const all = (await k.run('show run al', 'user')).text;
		const { modules } = await import('../src/engine/registry.js');
		const each = await Promise.all(modules.map((m) => k.run(`show ${m.name}`, 'user')));
		expect(all).toBe(each.map((r) => r.text).join('\n'));
		expect(all).toContain('set display tools open\n');
		expect(all).toContain('set display thinking closed\n');
		expect((await k.run('show running-config x', 'user')).text).toBe(
			'% nothing goes after show running-config: x'
		);
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

	it('renders the thinking and the reply as markdown by default, each plain on demand', async () => {
		const k = await page();
		expect(k.settings.get('display render', 'thinking')).toBe('markdown');
		expect(k.settings.get('display render', 'reply')).toBe('markdown');
		expect((await k.run('show display render', 'user')).text).toBe(
			['set display render thinking markdown', 'set display render reply markdown'].join('\n')
		);
		expect((await k.run('set display render tools plain', 'user')).text).toContain(
			'unknown display render "tools"'
		);
		expect((await k.run('set display render reply rich', 'user')).text).toContain(
			'not one of markdown plain'
		);
		expect((await k.run('set display render reply plain', 'user')).ok).toBe(true);
		const { display } = await import('../src/lib/display.svelte.js');
		expect(display.render).toEqual({ thinking: 'markdown', reply: 'plain' });
	});
});

describe('the headers of a server', () => {
	it('go with every request to it alone, after its key, as Name: value pairs', async () => {
		const k = await page();
		const sent: Record<string, Record<string, string>> = {};
		vi.stubGlobal(
			'fetch',
			vi.fn(async (url: string, init: RequestInit) => {
				sent[url] = init.headers as Record<string, string>;
				return models(['m']);
			})
		);
		await k.run(
			[
				'set endpoints url a http://a/v1',
				'set endpoints url b http://b/v1',
				'set endpoints key a sk-a',
				"set endpoints headers a 'anthropic-dangerous-direct-browser-access: true; x-id: 7'"
			].join('\n'),
			'user'
		);
		await k.run('show models', 'user');
		expect(sent['http://a/v1/models']).toEqual({
			Authorization: 'Bearer sk-a',
			'anthropic-dangerous-direct-browser-access': 'true',
			'x-id': '7'
		});
		expect(sent['http://b/v1/models']).toEqual({});
		expect((await k.run("set endpoints headers b 'no colon'", 'user')).text).toContain(
			'endpoints headers b: "no colon" is not Name: value'
		);
		expect((await k.run("set endpoints headers b 'a name: 1'", 'user')).text).toContain(
			'endpoints headers b: "a name: 1" is not Name: value'
		);
		expect((await k.run("set mcp headers m 'x: 1'\nset mcp url m http://m/mcp", 'user')).ok).toBe(
			true
		);
		const asked = user(REFUSE);
		expect((await k.run("set endpoints headers b 'x: 2'", 'llm', asked)).ok).toBe(true);
		expect((await k.run("set endpoints headers a 'x: 2'", 'llm', asked)).ok).toBe(true);
		expect(asked.asked).toEqual([]);
	});
});

describe('a typo', () => {
	it('gets the word it misses, a letter off or two letters swapped', async () => {
		const k = await page();
		const error = async (line: string) => (await k.run(line, 'user')).text;
		expect(await error('sow running')).toBe('% unknown command "sow", did you mean show');
		expect(await error('set chat modle x')).toBe(
			'% unknown key "chat modle", did you mean chat model'
		);
		expect(await error('set chta model x')).toBe('% unknown key "chta", did you mean chat');
		expect(await error('show runnign-config')).toBe(
			'% unknown word "runnign-config" after show, did you mean running-config'
		);
		expect(await error('set chat zzzzz x')).toBe('% unknown key "chat zzzzz"');
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
		expect((await k.run('set tools use repo.search off', 'user')).text).toBe(
			'% unknown tools "repo.search"'
		);
		const r = await k.run('set tools preview config query\nset tools use config off', 'user');
		expect(r.ok).toBe(true);
		expect((await k.run('show tools config', 'user')).text).toBe(
			'set tools preview config query\nset tools use config off'
		);
		expect((await k.run('no tools config', 'user')).ok).toBe(true);
		expect(k.settings.get('tools preview', 'repo.search')).toBeUndefined();
		expect(k.settings.get('tools use', 'repo.search')).toBe('consent');
	});
});

describe('a title', () => {
	it('renames the conversation of the batch with the batch, and no save keeps it', async () => {
		const k = await page();
		const c = { id: 'c', title: 'hello' };
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
		await k.run('copy running-config a', 'user');
		expect(localStorage.getItem('kiss.saves')).not.toContain('Bonjour');
	});
});

describe('a word after set, no or show', () => {
	it('competes with the modules, a prefix of both being ambiguous', async () => {
		const k = await page();
		expect((await k.run('show d', 'user')).text).toBe('% ambiguous word "d": diff display');
		expect((await k.run('show t', 'user')).text).toBe('% ambiguous word "t": title tools');
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
	it('asks the user every time the model opens a way out, whatever spells it', async () => {
		const k = await page();
		await k.run('set tools use config off\nset chat system mine', 'user');
		const once = user(ONCE);
		expect((await k.run('set chat system theirs', 'llm', once)).ok).toBe(true);
		expect((await k.run('set endpoints url b http://b/v1', 'llm', once)).ok).toBe(true);
		expect((await k.run('set endpoints url b http://c/v1', 'llm', once)).ok).toBe(true);
		expect(once.asked).toEqual([
			{ kind: 'change', lines: ['+ set endpoints url b http://b/v1'] },
			{
				kind: 'change',
				lines: ['- set endpoints url b http://b/v1', '+ set endpoints url b http://c/v1']
			}
		]);
		const refuse = user(REFUSE);
		expect((await k.run('no tools config', 'llm', refuse)).text).toBe(
			'% the user refused the change'
		);
		expect(refuse.asked).toEqual([
			{ kind: 'change', lines: ['- set tools use config off', '+ set tools use config on'] }
		]);
		expect(k.settings.get('tools use', 'config')).toBe('off');
	});

	it('checks an item the model names against the configuration as it applies, reaching no address it writes', async () => {
		const k = await page();
		const fetch = vi.fn(async (_url: string) => models(['x']));
		vi.stubGlobal('fetch', fetch);
		const r = await k.run(
			'set endpoints url z http://z/v1\nset models top_p z/x 0.5',
			'llm',
			user(ONCE)
		);
		expect(r.text).toContain('unknown models "z/x"');
		expect(fetch.mock.calls.map(([url]) => String(url))).not.toContainEqual(
			expect.stringContaining('http://z/')
		);
	});

	it('lets the model choose how an endpoint speaks, never where it goes', async () => {
		const k = await page();
		await k.run('set endpoints url a http://a/v1', 'user');
		const refuse = user(REFUSE);
		expect((await k.run('set endpoints protocol a messages', 'llm', refuse)).ok).toBe(true);
		expect(refuse.asked).toEqual([]);
		expect((await k.run('set endpoints url a http://b/v1', 'llm', refuse)).ok).toBe(false);
		expect(refuse.asked).toHaveLength(1);
	});

	it('never asks when the model closes', async () => {
		const k = await page();
		await k.run('set endpoints url a http://a/v1', 'user');
		const refuse = user(REFUSE);
		expect((await k.run('set tools use config off', 'llm', refuse)).ok).toBe(true);
		expect((await k.run('no endpoints a', 'llm', refuse)).ok).toBe(true);
		expect(refuse.asked).toEqual([]);
		expect((await k.run('set tools use config consent', 'llm', refuse)).ok).toBe(false);
	});

	it('offers always for a call alone, a change asking every time', () => {
		const call: Grant = { kind: 'call', tool: 'echo', args: '{}' };
		const change: Grant = { kind: 'change', lines: [] };
		expect([always(call), answers(call)]).toEqual(['turns echo on', [ONCE, ALWAYS, REFUSE]]);
		expect([always(change), answers(change)]).toEqual([null, [ONCE, REFUSE]]);
	});

	it('refuses when nobody is here, and a stop while it asks applies nothing', async () => {
		const k = await page();
		expect((await k.run('set endpoints url z http://z/v1', 'llm')).text).toBe(
			'% nobody is here to agree to the change'
		);
		const stop = new AbortController();
		let answer: ((v: Verdict) => void) | undefined;
		const grant = () => new Promise<Verdict>((resolve) => (answer = resolve));
		const r = k.run('set endpoints url z http://z/v1', 'llm', { signal: stop.signal, grant });
		await vi.waitFor(() => expect(answer).toBeDefined());
		stop.abort();
		answer!(ONCE);
		await expect(r).rejects.toThrow();
		expect(k.settings.get('endpoints url', 'z')).toBeUndefined();
	});

	it('asks the value of a secret left out, which never shows', async () => {
		const k = await page();
		await k.run('set mcp url a http://a/mcp', 'user');
		const asked: string[] = [];
		const secret = async (key: string) => (asked.push(key), 'sk-typed');
		expect((await k.run('set mcp key a', 'llm', { secret })).text).toBe(
			'! the user typed mcp key a\n+ ! mcp key a is set'
		);
		expect(asked).toEqual(['mcp key a']);
		expect(k.settings.get('mcp key', 'a')).toBe('sk-typed');
		expect((await k.run('set mcp key a', 'llm', { secret: async () => null })).text).toBe(
			'% no value given for mcp key a'
		);
		expect((await k.run('set mcp key a', 'llm')).text).toBe('% nobody is here to give mcp key a');
	});

	it('keeps the line that asks as written, and removes a value written out of view', async () => {
		const k = await page();
		expect(k.redact('set mcp key a')).toBe('set mcp key a');
		expect(k.redact('set mcp key a sk-x\nshow mcp')).toBe('set mcp key a <removed>\nshow mcp');
		expect(k.redact('set mcp url a http://a/mcp')).toBe('set mcp url a http://a/mcp');
	});

	it('takes the mark of a secret removed for no value, the asking line named', async () => {
		const k = await page();
		await k.run('set mcp url a http://a/mcp\nset mcp key a sk-a', 'user');
		const out = (await k.run('set mcp key a <removed>', 'llm')).text;
		expect(out).toContain('"<removed>" stands for a secret kept out of view, it is no value');
		expect(out).toContain('-> the line without a value asks the user');
		expect(k.settings.get('mcp key', 'a')).toBe('sk-a');
	});

	it('reads a secret replaced as one line, its values out of view', async () => {
		const k = await page();
		await k.run('set mcp url a http://a/mcp\nset mcp key a sk-a', 'user');
		expect((await k.run('set mcp key a sk-b', 'user')).text).toBe('! mcp key a changed');
		expect((await k.run('no mcp key a', 'user')).text).toBe('- ! mcp key a is set');
	});
});

// the conversations of a page, packed and unpacked for real, and a user who
// saves every file offered, picks the file given and answers every question
// to confirm alike, the questions kept
function shelf(list: Conversation[], picked: string | null = null, yes = true) {
	const offered: { name: string; text: string }[] = [];
	const asked: string[] = [];
	const conversations: Library = {
		list: () => list,
		pack: serialize,
		async unpack(text) {
			const found = fresh(parse(text), list);
			list.unshift(...found.added);
			return found;
		},
		find: (prefix) => named(list, prefix),
		async remove(ids) {
			list.splice(0, list.length, ...list.filter((c) => !ids.includes(c.id)));
		}
	};
	return {
		offered,
		asked,
		conversations,
		offer: async (name: string, text: string) => (offered.push({ name, text }), true),
		pick: async () => picked,
		confirm: async (question: string) => (asked.push(question), yes)
	};
}

// a conversation titled, holding one message
function talk(id: string, title: string, updated: number): Conversation {
	const c: Conversation = { id, title, updated, entries: [], leaf: null };
	append(c, null, { role: 'user', text: title });
	return c;
}

describe('an erase', () => {
	it('goes once the user confirms it, all at once, the pinned kept unless named', async () => {
		const k = await page();
		const list = () => [
			talk('ab12-1', 'one', 3),
			{ ...talk('ab34-2', 'kept', 2), pinned: true as const },
			talk('cd56-3', 'three', 1)
		];
		const all = shelf(list());
		expect((await k.run('erase all', 'llm', all)).text).toBe('- ab12-1 one\n- cd56-3 three');
		expect(all.asked).toEqual(['Erase 2 conversations?']);
		expect(all.conversations.list().map((c) => c.id)).toEqual(['ab34-2']);
		const pinned = shelf(list());
		expect((await k.run('erase ab3', 'user', pinned)).text).toBe('- ab34-2 kept');
		expect(pinned.asked).toEqual(['Erase kept?']);
		const kept = shelf(list(), null, false);
		expect((await k.run('erase all', 'llm', kept)).text).toBe('% the user erased nothing');
		expect(kept.conversations.list()).toHaveLength(3);
		expect((await k.run('erase ab', 'user', shelf(list()))).text).toBe(
			'% ambiguous conversation "ab": ab12-1 ab34-2'
		);
		expect((await k.run('erase all', 'user', shelf([]))).text).toBe('% no conversation to erase');
		expect((await k.run('erase all', 'user')).text).toBe('% nobody is here to confirm the erase');
	});

	it('takes a configuration whole before a conversation of the same word', async () => {
		const k = await page();
		await k.run('set chat system blue', 'user');
		await k.run('copy running-config ab12', 'user');
		const list = [talk('ab12-1', 'one', 1)];
		const s = shelf(list, null, false);
		expect((await k.run('erase ab12', 'llm', s)).text).toBe('% the user erased nothing');
		const yes = shelf(list);
		expect((await k.run('erase ab12', 'llm', yes)).text).toBe('! erased ab12');
		expect((await k.run('erase run', 'llm', yes)).text).toBe(
			'- set chat system blue\n+ set chat system ""'
		);
		expect([...s.asked, ...yes.asked]).toEqual([
			'Erase ab12?',
			'Erase ab12?',
			'Erase running-config?'
		]);
		expect((await k.run('show saves', 'user')).text).toBe('! nothing saved yet');
		expect((await k.run('erase ab12', 'llm', yes)).text).toBe('- ab12-1 one');
	});
});

describe('the conversations', () => {
	it('list newest first, the one the batch was sent in marked', async () => {
		const k = await page();
		const list = [talk('bbbbbbbb-2', 'Two words', 2), talk('aaaaaaaa-1', 'one', 1)];
		const r = await k.run('show conversations', 'llm', { ...shelf(list), conversation: list[1] });
		expect(r.text).toBe(
			[`  bbbbbbbb ${localTime(2)} "Two words"`, `* aaaaaaaa ${localTime(1)} one`].join('\n')
		);
		expect((await k.run('show conversations', 'llm', shelf([]))).text).toBe(
			'! no conversation yet'
		);
	});

	it('export the one the batch was sent in, another by a prefix of its id, all, or the configuration', async () => {
		const k = await page();
		const list = [talk('ab12-2', 'Two', 2), talk('ab34-1', 'One', 1)];
		const s = shelf(list);
		expect((await k.run('export', 'llm', { ...s, conversation: list[1] })).text).toBe(
			'! exported 1 conversation to One.json'
		);
		expect((await k.run('export ab1', 'llm', s)).text).toBe(
			'! exported 1 conversation to Two.json'
		);
		expect((await k.run('export all', 'llm', s)).text).toMatch(
			/^! exported 2 conversations to all \d{4}-\d\d-\d\d \d\d-\d\d\.json$/
		);
		expect(s.offered.map((o) => parse(o.text).map((c) => c.id))).toEqual([
			['ab34-1'],
			['ab12-2'],
			['ab12-2', 'ab34-1']
		]);
		expect((await k.run('export ab', 'llm', s)).text).toBe(
			'% ambiguous conversation "ab": ab12-2 ab34-1'
		);
		expect((await k.run('export zz', 'llm', s)).text).toBe('% unknown conversation "zz"');
		expect((await k.run('export', 'llm', s)).text).toBe('% no conversation here');
		expect((await k.run('export all\nshow title', 'llm', s)).text).toContain('runs alone');
		const refused = { ...s, offer: async () => false };
		expect((await k.run('export all', 'llm', refused)).text).toBe('% the user saved no file');
		await k.run('set endpoints url a http://a/v1\nset endpoints key a sk-a', 'user');
		const shown = await k.run('export running-config', 'llm', s);
		expect(shown.text).toMatch(
			/^! exported running-config to running-config \d{4}-\d\d-\d\d \d\d-\d\d\.conf$/
		);
		expect(shown.text).not.toContain('sk-a');
		const conf = s.offered.at(-1)!;
		expect(shown.text).toContain(conf.name);
		expect(conf.text).toContain('set endpoints url a http://a/v1\n');
		expect(conf.text).toContain('set endpoints key a sk-a\n');
		const k2 = await page();
		expect((await k2.run('import', 'user', shelf([], conf.text))).text).not.toContain('sk-a');
		expect((await k2.run('show running-config', 'user')).text).toBe(
			(await k.run('show running-config', 'user')).text
		);
		await k.run('copy run ab12', 'user');
		await k.run('set endpoints key a sk-b', 'user');
		expect((await k.run('export ab12', 'llm', s)).text).toMatch(/^! exported ab12 to ab12 /);
		expect(s.offered.at(-1)!.text).toContain('set endpoints key a sk-a\n');
		expect((await k.run('export start', 'llm', s)).text).toMatch(/^! exported startup-config /);
	});

	it('import the conversations of a file beside the others, or run a configuration whole', async () => {
		const k = await page();
		const here = talk('aaaaaaaa-1', 'Here', 1);
		const file = serialize([talk('bbbbbbbb-2', 'New', 2), here]);
		const list = [here];
		expect((await k.run('import', 'llm', shelf(list, file))).text).toBe(
			['+ bbbbbbbb New', '! aaaaaaaa Here is already here'].join('\n')
		);
		expect(list.map((c) => c.id)).toEqual(['bbbbbbbb-2', 'aaaaaaaa-1']);
		expect((await k.run('import', 'llm', shelf(list, '{"kiss":"save"}'))).text).toBe(
			'% file.kiss: not conversations'
		);
		expect(list).toHaveLength(2);
		expect((await k.run('import', 'llm', shelf(list))).text).toBe('% the user picked no file');
		const conf = 'set chat system "from a file"\n! a note\n set display tools open\n';
		expect((await k.run('import', 'user', shelf(list, conf))).text).toContain(
			'+ set chat system "from a file"'
		);
		expect(k.settings.get('display tools')).toBe('open');
		expect((await k.run('import', 'user', shelf(list, conf))).text).toBe(
			'! ran 2 lines of the file'
		);
		expect((await k.run('import', 'user', shelf(list, '! only a note\n'))).text).toBe(
			'% the file holds no line'
		);
		expect((await k.run('import', 'user', shelf(list, serialize([])))).text).toBe(
			'% the file holds no conversation'
		);
		expect((await k.run('set display tools open', 'user')).text).toBe('! no change');
		const broken = 'set chat system other\nbogus';
		expect((await k.run('import', 'user', shelf(list, broken))).text).toContain('file line 2:');
		expect(k.settings.get('chat system')).toBe('from a file');
	});
});

describe('kiss.conf', () => {
	it('applies whole or not at all, the rules of every module holding', async () => {
		vi.resetModules();
		const k = await import('../src/engine/run.js');
		expect(k.defaults('set chat system red\nset endpoints key s sk-site')).toEqual([
			'endpoints: endpoints s has no url'
		]);
		expect(k.defaults('set chat system red\nshow running-config')).toEqual([
			'show running-config: kiss.conf holds set lines only'
		]);
		k.start();
		expect(k.settings.get('chat system')).toBe('');
		expect((await k.run('set chat system blue', 'user')).ok).toBe(true);
	});
});

describe('the archive', () => {
	it('stays as it was when the browser refuses to store it', async () => {
		const k = await page();
		await k.run('copy running-config one', 'user');
		vi.stubGlobal('localStorage', {
			setItem() {
				throw new Error('quota');
			}
		});
		expect((await k.run('copy running-config two', 'user')).text).toContain('refuses to store');
		vi.unstubAllGlobals();
		expect((await k.run('show saves', 'user')).text).not.toContain('two');
		expect((await k.run('copy running-config three', 'user')).text).toBe(
			'! copied running-config to three'
		);
	});
});

describe('a model', () => {
	it('takes the name a server gives it, and numbers within their bounds', async () => {
		const k = await page();
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => models(['org/m:tag']))
		);
		await k.run('set endpoints url hf http://hf/v1', 'user');
		expect((await k.run('set models temperature hf/org/m:tag 0.70', 'user')).ok).toBe(true);
		expect(k.settings.get('models temperature', 'hf/org/m:tag')).toBe('0.7');
		expect((await k.run('set models top_p a/b 2', 'user')).text).toContain('"2" is above 1');
		expect((await k.run('set models top_k a/b 1.5', 'user')).text).toContain('not a whole number');
	});
});

describe('a copy', () => {
	it('warns when no model answers the chat, and once of an endpoint that fails', async () => {
		const k = await page();
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => models(['m']))
		);
		await k.run('set endpoints url a http://a/v1\nset endpoints url b http://b/v1', 'user');
		expect((await k.run('copy running-config a', 'user')).text).toBe(
			'! copied running-config to a\n! chat model is not set -> /show models, then /set chat model <endpoint/model>'
		);
		await k.run('set chat model b/x', 'user');
		expect((await k.run('copy running-config a', 'user')).text).toBe(
			'! copied running-config to a\n! b does not serve x, see show models'
		);
		await k.run('set chat model b/m', 'user');
		expect((await k.run('copy running-config a', 'user')).text).toBe(
			'! copied running-config to a'
		);
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => Response.json({ error: { message: 'version required' } }, { status: 400 }))
		);
		// an endpoint that fails warns once, by endpoints, never again by chat
		const warnings = (await k.run('copy running-config a', 'user')).text.split('\n').slice(1);
		expect(warnings).toHaveLength(1);
		expect(warnings[0]).toMatch(/^! endpoints a: .* version required, endpoints b: /);
	});

	it('refuses a parameter the protocol of its endpoint never sends, a protocol that leaves one too', async () => {
		const k = await page();
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => models(['x']))
		);
		await k.run('set endpoints url a http://a/v1\nset endpoints protocol a messages', 'user');
		expect((await k.run('set models seed a/x 7', 'user')).text).toBe(
			'% models: models seed a/x: messages never sends it -> /no models seed a/x'
		);
		await k.run('set endpoints protocol a chat\nset models seed a/x 7', 'user');
		expect((await k.run('set endpoints protocol a messages', 'user')).ok).toBe(false);
		expect((await k.run('no models seed a/x\nset endpoints protocol a messages', 'user')).ok).toBe(
			true
		);
	});

	it('copies from a configuration to another, a save of the same name giving way', async () => {
		const k = await page();
		expect((await k.run('copy running-config', 'user')).text).toBe(
			'running-config\nstartup-config\n<name>'
		);
		expect((await k.run('copy running-config running-config', 'user')).text).toContain(
			'from one configuration to another'
		);
		expect((await k.run('copy running-config a b', 'user')).text).toBe(
			'% nothing goes after copy running-config a: b'
		);
		vi.useFakeTimers({ now: new Date('2026-10-05T09:17:00Z'), toFake: ['Date'] });
		await k.run('set chat system blue', 'user');
		await k.run('copy running-config a', 'user');
		vi.setSystemTime(new Date('2026-10-05T09:20:00Z'));
		await k.run('copy running-config b', 'user');
		vi.setSystemTime(new Date('2026-10-05T09:25:00Z'));
		await k.run('set chat system green', 'user');
		await k.run('copy running-config a', 'user');
		vi.useRealTimers();
		expect((await k.run('show saves', 'user')).text).toBe('b 2026-10-05 11:20\na 2026-10-05 11:25');
		await k.run('set endpoints url m http://m/v1\nset endpoints key m sk-m', 'user');
		await k.run('copy running-config c', 'user');
		expect((await k.run('show saves b', 'user')).text).toBe('set chat system blue');
		expect((await k.run('show saves c', 'user')).text).toBe(
			['set chat system green', '! endpoints key m is set', 'set endpoints url m http://m/v1'].join(
				'\n'
			)
		);
		expect((await k.run('show saves c | include url', 'user')).text).toBe(
			'set endpoints url m http://m/v1'
		);
		expect((await k.run('show saves z', 'user')).text).toBe('% unknown save "z"');
		const no = shelf([], null, false);
		expect((await k.run('copy a startup-config', 'llm', no)).text).toBe(
			'% the user copied nothing'
		);
		const yes = shelf([]);
		expect((await k.run('copy b a', 'llm', yes)).text).toBe('! copied b to a');
		expect([...no.asked, ...yes.asked]).toEqual([
			'Copy a to startup-config?',
			'Copy b to a, replacing it?'
		]);
		expect((await k.run('show saves a', 'user')).text).toBe('set chat system blue');
	});

	it('compares one configuration with another', async () => {
		const k = await page();
		await k.run('set chat system blue', 'user');
		await k.run('copy running-config a', 'user');
		await k.run('set chat system green', 'user');
		expect((await k.run('show diff a running-config', 'user')).text).toBe(
			'- set chat system blue\n+ set chat system green'
		);
		expect((await k.run('show diff startup-config a', 'user')).text).toBe('+ set chat system blue');
		expect((await k.run('show diff running-config running-config', 'user')).text).toBe(
			'! no difference'
		);
		expect((await k.run('show diff a', 'user')).text).toBe('a\nrunning-config\nstartup-config');
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
				'set endpoints protocol a chat',
				'set endpoints timeout a 120',
				'set endpoints url a http://a/v1',
				'set endpoints protocol b chat',
				'set endpoints timeout b 120',
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
			'! endpoints key a is set\nset endpoints protocol a chat\nset endpoints timeout a 120\nset endpoints url a http://a/v1'
		);
		expect((await k.run('show chat zz', 'user')).text).toContain('unknown key');
	});
});

describe('a listing by group', () => {
	it('pastes back as it is, its headers skipped as notes', async () => {
		const k = await page();
		const shown = (await k.run('show tools', 'user')).text;
		expect(shown).toContain('! KiSS\nset tools use config on');
		expect((await k.run(shown, 'user')).ok).toBe(true);
	});
});

describe('style', () => {
	it('is the last style sheet of the page, its sheets by name, gone with the running-config', async () => {
		const k = await page();
		await k.run(
			`set style sheet b 'b { color: red }'\nset style sheet a 'a { color: blue }'`,
			'user'
		);
		const last = () => document.head.querySelector('style:last-of-type')!.textContent;
		expect(last()).toBe('a { color: blue }\nb { color: red }');
		await k.run('erase running-config', 'user', shelf([]));
		expect(last()).toBe('');
	});

	it('keys the tokens of the page of a value of their own, the model free on them and on sheets', async () => {
		const k = await page();
		const shown = (await k.run('show style', 'user')).text;
		expect(shown).toContain('set style bg "oklch(0.17 0.005 260)"');
		expect(shown).toContain('set style size-secondary 0.8rem');
		expect(shown).not.toMatch(/style (ok|surface|text-secondary|icon) /);
		const refuse = user(REFUSE);
		expect((await k.run('set style accent red', 'llm', refuse)).ok).toBe(true);
		expect(refuse.asked).toEqual([]);
		const tokens = document.head.querySelector('style:nth-last-of-type(2)')!;
		expect((tokens as HTMLStyleElement).sheet!.cssRules[0].cssText).toContain('--accent: red');
		expect((await k.run(`set style sheet x '.tool { display: none }'`, 'llm', refuse)).ok).toBe(
			true
		);
		expect(refuse.asked).toEqual([]);
	});
});

describe('erase and copy', () => {
	it('erase running-config drops what the session sets, a copy to it puts back the save it names', async () => {
		const k = await page('set display tools open');
		expect((await k.run('copy first running-config', 'user')).text).toContain(
			'unknown save "first"'
		);
		await k.run('set chat system blue', 'user');
		await k.run('copy running-config first', 'user');
		await k.run('set chat system green', 'user');
		await k.run('copy running-config second', 'user');
		await k.run('set chat system pink\nset display tools closed', 'user');
		await k.run('erase running-config', 'user', shelf([]));
		expect(k.settings.get('chat system')).toBe('');
		expect(k.settings.get('display tools')).toBe('open');
		await k.run('copy second running-config', 'user');
		expect(k.settings.get('chat system')).toBe('green');
		await k.run('copy first running-config', 'user');
		expect(k.settings.get('chat system')).toBe('blue');
		// an item no key takes anymore goes as an unknown key does
		const values = { 'chat system': 'old', 'privilege level gone': 'allow' };
		localStorage.setItem('kiss.saves', JSON.stringify([{ name: 'old', date: '', values }]));
		const later = await page();
		expect((await later.run('show diff old run', 'user')).text).toBe('- set chat system old');
		expect((await later.run('copy old running-config', 'user')).text).toBe(
			'! dropped unknown keys: privilege level gone\n- set chat system ""\n+ set chat system old'
		);
	});
});

describe('a page', () => {
	it('starts with the startup-config over the kiss.conf of the moment, whatever a save does', async () => {
		const k = await page();
		await k.run('set chat system blue', 'user');
		expect((await k.run('copy run start', 'user')).text).toBe(
			'! copied running-config to startup-config'
		);
		await k.run('set chat system green', 'user');
		await k.run('copy running-config b', 'user');
		await k.run('copy b running-config', 'user');
		const next = await page('set display tools open');
		expect(next.settings.get('chat system')).toBe('blue');
		expect(next.settings.get('display tools')).toBe('open');
		expect((await next.run('show startup-config', 'user')).text).toBe(
			'set chat system blue\nset display tools open'
		);
		expect((await next.run('show diff start run', 'user')).text).toBe('! no difference');
		await next.run('copy b startup-config', 'user');
		expect((await page()).settings.get('chat system')).toBe('green');
		expect((await (await page()).run('erase startup-config', 'user', shelf([]))).text).toBe(
			'! erased startup-config, the page starts on kiss.conf'
		);
		const bare = await page('set display tools open');
		expect(bare.settings.get('chat system')).toBe('');
		expect((await bare.run('show startup-config', 'user')).text).toBe('set display tools open');
		expect((await (await page()).run('show startup-config', 'user')).text).toBe(
			'! every key is at its default'
		);
	});
});

describe('every key', () => {
	// the item a named key of a module takes in these laws, m unless told: the
	// keys of endpoints go to n, so m keeps the protocol every parameter goes by
	const ITEMS: Record<string, string> = {
		endpoints: 'n',
		models: 'm/x',
		tools: 'config',
		mcp: 'b'
	};
	// values to try for a key by its kind, the first it takes that is not its
	// default kept
	const TRIES: Record<string, string[]> = {
		string: ['m/x', 'X-A: 1'],
		url: ['http://n/v1'],
		css: ['#123456', '2px', '700', 'serif', '1.5']
	};

	// a page with an endpoint m serving x, and one line set for every key that
	// takes a value written out, each the first value it takes
	async function full() {
		const k = await page();
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => models(['x']))
		);
		await k.run('set endpoints url m http://m/v1', 'user');
		const { modules } = await import('../src/engine/registry.js');
		const untaken: string[] = [];
		for (const m of modules) {
			for (const [name, def] of Object.entries(m.keys)) {
				if (def.kind === 'secret') continue;
				const item = def.named ? ` ${def.names?.(modules)[0] ?? ITEMS[m.name] ?? 'm'}` : '';
				const bounds = [def.min, def.max, 1, 2].filter((n) => n !== undefined).map(String);
				const tries =
					def.kind === 'enum' ? [...def.values!] : def.kind === 'number' ? bounds : TRIES[def.kind];
				const line = await (async () => {
					for (const value of tries.filter((v) => v !== def.default)) {
						const l = `set ${m.name} ${name}${item} ${JSON.stringify(value)}`;
						const r = await k.run(l, 'user');
						if (r.ok && r.text.split('\n').some((o) => o.startsWith('+ '))) return l;
					}
				})();
				if (!line) untaken.push(`${m.name} ${name}${item}`);
			}
		}
		expect(untaken).toEqual([]);
		return k;
	}

	it('pastes back after no as show running-config writes it, and goes alone, or a rule of its module says why', async () => {
		const k = await full();
		const running = (await k.run('show running-config', 'user')).text;
		const lines = running.split('\n').filter((l) => l.startsWith('set '));
		for (const line of lines) {
			const r = await k.run(`no ${line.slice('set '.length)}`, 'user');
			if (r.ok) {
				const after = (await k.run('show running-config', 'user')).text.split('\n');
				expect(after).not.toContain(line);
				expect(lines.filter((l) => l !== line && !after.includes(l))).toEqual([]);
				await k.run(line, 'user');
			} else {
				expect(r.text).toMatch(/^% [a-z]+: /);
			}
		}
	});

	it('names only an item its source serves, one already held whatever the source says', async () => {
		const k = await full();
		const { modules } = await import('../src/engine/registry.js');
		const known = modules.filter((m) => m.items).map((m) => m.name);
		expect(known.sort()).toEqual(['display', 'models', 'tools']);
		for (const line of [
			'set models max_tokens m/nope 5',
			'set models max_tokens nowhere/x 5',
			'set tools use nope on',
			'set display render nope plain'
		]) {
			expect((await k.run(line, 'user')).text).toMatch(/^% unknown [a-z ]+"[a-z/]+"/);
		}
		expect((await k.run('set models max_tokens m/xy 5', 'user')).text).toBe(
			'% unknown models "m/xy", did you mean m/x'
		);
		vi.stubGlobal(
			'fetch',
			vi.fn(async (url: string) =>
				url.startsWith('http://m/')
					? Response.json({ error: { message: 'down' } }, { status: 503 })
					: models(['x'])
			)
		);
		expect((await k.run('set models top_p m/y 0.5', 'user')).text).toMatch(
			/^% unknown models "m\/y", endpoints m: .*answers 503 down$/
		);
		expect((await k.run('set models top_p m/x 0.5', 'user')).ok).toBe(true);
		expect((await k.run('set chat model nowhere/x', 'user')).text).toContain(
			'chat model nowhere/x names no endpoint'
		);
		expect((await k.run('set chat model m/anything', 'user')).ok).toBe(true);
		await k.run('set chat model n/x', 'user');
		expect((await k.run('no endpoints m', 'user')).text).toBe(
			'% models: models m/x names no endpoint -> /no models m/x'
		);
	});
});
