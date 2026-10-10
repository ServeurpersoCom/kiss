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
		expect((await k.run('show running now', 'user')).text).toBe(
			'% nothing goes after show running: now'
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
		expect((await slow).text).toBe('! endpoints a\n ! a/m');
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
		expect((await r).text).toBe('! endpoints a down\n! endpoints b\n ! b/m\n ! b/org/n');
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
		expect((await k.run('set endpoints url fresh http://f/v1', 'llm', { grant })).ok).toBe(true);
		expect((await k.run('set endpoints url u http://u2/v1', 'user')).ok).toBe(true);
	});

	it('never prints', async () => {
		const k = await page();
		await k.run('set endpoints url u http://u/v1\nset endpoints key u sk-user', 'user');
		const shown = (await k.run('show running', 'user')).text;
		expect(shown).toContain('! endpoints key u is set');
		expect(shown).not.toContain('sk-user');
		expect(k.redact('set endpoints key u sk-user')).toBe('set endpoints key u <removed>');
		expect(k.redact('set endpoints key u "sk-user')).toBe('set endpoints key u <removed>');
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

	it('renders the thinking and the reply as markdown by default, each plain on demand', async () => {
		const k = await page();
		expect(k.settings.get('display render', 'thinking')).toBe('markdown');
		expect(k.settings.get('display render', 'reply')).toBe('markdown');
		expect((await k.run('show display render', 'user')).text).toBe(
			['set display render thinking markdown', 'set display render reply markdown'].join('\n')
		);
		expect((await k.run('set display render tools plain', 'user')).text).toContain(
			'tools is not a block of text'
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
		const r = await k.run("set endpoints headers b 'x: 2'", 'llm', { grant: asked.grant });
		expect(r.text).toContain('the user refused the change');
		expect(asked.asked).toHaveLength(1);
		expect((await k.run("set endpoints headers a 'x: 2'", 'llm', { grant })).text).toContain(
			'endpoints a holds a secret, only the user changes it'
		);
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
		expect(await error('show runnign')).toBe(
			'% unknown word "runnign" after show, did you mean running'
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
		await k.run('save a', 'user');
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
	it('asks the user before the model changes a guarded key, whatever spells it', async () => {
		const k = await page();
		await k.run('set tools use t off\nset chat system mine', 'user');
		const once = user(ONCE);
		expect((await k.run('set chat system theirs', 'llm', once)).ok).toBe(true);
		expect(once.asked).toEqual([
			{
				kind: 'change',
				lines: ['- set chat system mine', '+ set chat system theirs'],
				allows: ['chat']
			}
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

	it('goes as far as the privilege of the module, a privilege changing on a yes every time', async () => {
		const k = await page();
		const once = user(ONCE);
		await k.run('set privilege level chat deny', 'user');
		expect((await k.run('set chat system x', 'llm', once)).text).toBe(
			'% the privilege of chat denies the change'
		);
		await k.run('set privilege level chat allow', 'user');
		expect((await k.run('set chat system x', 'llm', once)).ok).toBe(true);
		expect(once.asked).toEqual([]);
		const refuse = user(REFUSE);
		expect((await k.run('set privilege level chat ask', 'llm', refuse)).text).toBe(
			'% the user refused the change'
		);
		expect((await k.run('reset', 'llm', refuse)).text).toBe('% the user refused the change');
		expect(refuse.asked).toEqual([
			{
				kind: 'change',
				lines: ['- set privilege level chat allow', '+ set privilege level chat ask'],
				allows: []
			},
			{
				kind: 'change',
				lines: ['- set privilege level chat allow', '+ set privilege level chat ask'],
				allows: []
			}
		]);
		expect((await k.run('set privilege level chat ask', 'llm', user(ALWAYS))).ok).toBe(true);
		expect(k.settings.get('privilege level', 'chat')).toBe('ask');
		expect(k.settings.names('privilege')).toEqual(['chat']);
		expect((await k.run('set privilege level privilege allow', 'user')).text).toContain(
			'privilege takes no privilege'
		);
		expect((await k.run('set privilege level display allow', 'user')).text).toContain(
			'display takes no privilege'
		);
		expect((await k.run('show privilege', 'user')).text).toBe(
			[
				'set privilege level chat ask',
				'set privilege level css ask',
				'set privilege level endpoints ask',
				'set privilege level mcp ask',
				'set privilege level tools ask'
			].join('\n')
		);
	});

	it('offers always only when it grants something, and says what', () => {
		const call: Grant = { kind: 'call', tool: 'echo', args: '{}' };
		const change: Grant = { kind: 'change', lines: [], allows: ['chat', 'mcp'] };
		const privilege: Grant = { kind: 'change', lines: [], allows: [] };
		expect([always(call), answers(call)]).toEqual(['turns echo on', [ONCE, ALWAYS, REFUSE]]);
		expect([always(change), answers(change)]).toEqual(['allows chat, mcp', [ONCE, ALWAYS, REFUSE]]);
		expect([always(privilege), answers(privilege)]).toEqual([null, [ONCE, REFUSE]]);
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

describe('a delete', () => {
	it('goes once the user confirms it, all at once, the pinned kept unless named', async () => {
		const k = await page();
		const list = () => [
			talk('ab12-1', 'one', 3),
			{ ...talk('ab34-2', 'kept', 2), pinned: true as const },
			talk('cd56-3', 'three', 1)
		];
		const all = shelf(list());
		expect((await k.run('delete all', 'llm', all)).text).toBe('- ab12-1 one\n- cd56-3 three');
		expect(all.asked).toEqual(['Delete 2 conversations?']);
		expect(all.conversations.list().map((c) => c.id)).toEqual(['ab34-2']);
		const pinned = shelf(list());
		expect((await k.run('delete ab3', 'user', pinned)).text).toBe('- ab34-2 kept');
		expect(pinned.asked).toEqual(['Delete kept?']);
		const kept = shelf(list(), null, false);
		expect((await k.run('delete all', 'llm', kept)).text).toBe('% the user deleted nothing');
		expect(kept.conversations.list()).toHaveLength(3);
		expect((await k.run('delete ab', 'user', shelf(list()))).text).toBe(
			'% ambiguous conversation "ab": ab12-1 ab34-2'
		);
		expect((await k.run('delete all', 'user', shelf([]))).text).toBe('% no conversation to delete');
		expect((await k.run('delete all', 'user')).text).toBe('% nobody is here to confirm the delete');
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
			/^! exported 2 conversations to kiss \d{4}-\d\d-\d\d\.json$/
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
		expect((await k.run('export running', 'llm', s)).text).toBe(
			'! exported the running configuration to kiss.conf'
		);
		const conf = s.offered.at(-1)!;
		expect(conf.name).toBe('kiss.conf');
		expect(conf.text).toContain('set endpoints url a http://a/v1\n');
		expect(conf.text).not.toContain('sk-a');
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
		expect(k.defaults('set chat system red\nshow running')).toEqual([
			'show running: kiss.conf holds set lines only'
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
		expect((await k.run('save three', 'user')).text).toBe('! saved three');
	});
});

describe('a model', () => {
	it('takes the name a server gives it, and numbers within their bounds', async () => {
		const k = await page();
		expect((await k.run('set models temperature hf/org/m:tag 0.70', 'user')).ok).toBe(true);
		expect(k.settings.get('models temperature', 'hf/org/m:tag')).toBe('0.7');
		expect((await k.run('set models top_p a/b 2', 'user')).text).toContain('"2" is above 1');
		expect((await k.run('set models top_k a/b 1.5', 'user')).text).toContain('not a whole number');
	});
});

describe('a save', () => {
	it('warns when no model answers the chat, and once of an endpoint that fails', async () => {
		const k = await page();
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => models(['m']))
		);
		await k.run('set endpoints url a http://a/v1\nset endpoints url b http://b/v1', 'user');
		expect((await k.run('save a', 'user')).text).toBe(
			'! saved a\n! chat model is not set -> /show models, then /set chat model <endpoint/model>'
		);
		await k.run('set chat model b/x', 'user');
		expect((await k.run('save a', 'user')).text).toBe(
			'! saved a\n! b does not serve x, see show models'
		);
		await k.run('set chat model b/m', 'user');
		expect((await k.run('save a', 'user')).text).toBe('! saved a');
		vi.stubGlobal(
			'fetch',
			vi.fn(async () => Response.json({ error: { message: 'version required' } }, { status: 400 }))
		);
		// an endpoint that fails warns once, by endpoints, never again by chat
		const warnings = (await k.run('save a', 'user')).text.split('\n').slice(1);
		expect(warnings).toHaveLength(1);
		expect(warnings[0]).toMatch(/^! endpoints a: .* version required, endpoints b: /);
	});

	it('takes one name, never session, and a save of the same name gives way', async () => {
		const k = await page();
		expect((await k.run('save', 'user')).text).toBe('<name>');
		expect((await k.run('save session', 'user')).text).toContain('is not a save name');
		expect((await k.run('save a b', 'user')).text).toBe('% nothing goes after save a: b');
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
				'set endpoints timeout a 120',
				'set endpoints url a http://a/v1',
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
			'! endpoints key a is set\nset endpoints timeout a 120\nset endpoints url a http://a/v1'
		);
		expect((await k.run('show chat zz', 'user')).text).toContain('unknown key');
	});
});

describe('a section', () => {
	it('keeps a line at the margin with the lines indented under it, when that line matches', async () => {
		const k = await page();
		expect((await k.run('show tools | section KiSS', 'user')).text).toBe(
			['! KiSS', ' set tools use config on'].join('\n')
		);
		expect((await k.run('show tools | section rounds', 'user')).text).toBe('set tools rounds 25');
	});

	it('reads back as set lines, indented or not', async () => {
		const k = await page();
		const shown = (await k.run('show tools', 'user')).text;
		expect(shown).toContain('\n set tools use config on');
		expect((await k.run(shown, 'user')).ok).toBe(true);
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

	it('keys the tokens of the page of a value of their own, the model free on them alone', async () => {
		const k = await page();
		const shown = (await k.run('show css', 'user')).text;
		expect(shown).toContain('set css bg "oklch(0.17 0.005 260)"');
		expect(shown).toContain('set css size-secondary 0.8rem');
		expect(shown).not.toMatch(/css (ok|surface|text-secondary|icon) /);
		const refuse = user(REFUSE);
		expect((await k.run('set css accent red', 'llm', refuse)).ok).toBe(true);
		expect(refuse.asked).toEqual([]);
		const tokens = document.head.querySelector('style:nth-last-of-type(2)')!;
		expect((tokens as HTMLStyleElement).sheet!.cssRules[0].cssText).toContain('--accent: red');
		expect((await k.run(`set css sheet x '.tool { display: none }'`, 'llm', refuse)).text).toBe(
			'% the user refused the change'
		);
	});
});

describe('reset and load', () => {
	it('reset drops what the session sets, load puts back the save it names', async () => {
		const k = await page('set display tools open');
		expect((await k.run('load first', 'user')).text).toContain('unknown save "first"');
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
