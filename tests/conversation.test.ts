import { describe, expect, it } from 'vitest';
import type { Conversation, Entry } from '../src/lib/types.js';
import { ago, stamp } from '../src/lib/config.js';
import {
	append,
	drop,
	forks,
	fresh,
	latest,
	parse,
	path,
	serialize,
	source
} from '../src/lib/conversation.js';

// a conversation holding one turn of each kind, a call left without its outcome
function demo(): Conversation {
	const c: Conversation = {
		id: 'c',
		title: 'Demo',
		updated: 1760000000000,
		entries: [],
		leaf: null
	};
	const user = append(c, null, { role: 'user', text: 'translate hi' });
	const assistant = append(c, user.id, {
		role: 'assistant',
		rounds: [
			{
				reasoning: 'think',
				text: 'first',
				calls: [
					{
						id: 'a',
						name: 'bash',
						args: '{"c":"ls"}',
						result: 'out',
						ok: true,
						images: [{ mime: 'image/png', data: 'AAAA' }]
					},
					{ id: 'b', name: 'bash', args: '{}' }
				]
			},
			{ reasoning: 'again', text: '', calls: [] },
			{ reasoning: '', text: 'salut', calls: [] }
		],
		error: 'stopped'
	});
	append(c, assistant.id, { role: 'cli', input: 'show title', output: 'title Demo', ok: true });
	return c;
}

const texts = (entries: Entry[]) => entries.map((e) => (e.role === 'user' ? e.text : e.role));

describe('a message copied', () => {
	it('gives its source: the text typed, the answer without thinking or calls, the output', () => {
		const [user, assistant, cli] = demo().entries;
		expect(source(user)).toBe('translate hi');
		expect(source(assistant)).toBe('first\n\nsalut');
		expect(source(cli)).toBe('title Demo');
	});
});

describe('an edit', () => {
	it('opens a branch beside the entry it edits, the branch edited kept', () => {
		const c: Conversation = { id: 'c', title: 't', updated: 0, entries: [], leaf: null };
		const one = append(c, null, { role: 'user', text: 'one' });
		append(c, one.id, { role: 'assistant', rounds: [] });
		const uno = append(c, null, { role: 'user', text: 'uno' });
		append(c, uno.id, { role: 'assistant', rounds: [] });
		expect(texts(path(c))).toEqual(['uno', 'assistant']);
		expect(c.entries).toHaveLength(4);
		expect(forks(c).get(null)).toEqual([one, uno]);
	});

	it('lets a version come back as it was last written in', () => {
		const c: Conversation = { id: 'c', title: 't', updated: 0, entries: [], leaf: null };
		const one = append(c, null, { role: 'user', text: 'one' });
		const answer = append(c, one.id, { role: 'assistant', rounds: [] });
		const uno = append(c, null, { role: 'user', text: 'uno' });
		const reply = append(c, uno.id, { role: 'assistant', rounds: [] });
		expect(latest(c, one.id)).toBe(answer.id);
		c.leaf = latest(c, one.id);
		const more = append(c, answer.id, { role: 'user', text: 'more' });
		expect(latest(c, one.id)).toBe(more.id);
		expect(latest(c, uno.id)).toBe(reply.id);
		expect(texts(path(c))).toEqual(['one', 'assistant', 'more']);
	});
});

describe('a message', () => {
	it('enters with the moment it entered', () => {
		const c: Conversation = { id: 'c', title: 't', updated: 0, entries: [], leaf: null };
		const before = Date.now();
		const one = append(c, null, { role: 'user', text: 'one' });
		expect(one.time).toBeGreaterThanOrEqual(before);
		expect(one.time).toBeLessThanOrEqual(Date.now());
	});

	it('reads how long ago it entered in the language of the browser, its moment in full', () => {
		const now = new Date(2026, 9, 9, 14, 37).getTime();
		const back = (seconds: number) => ago(now - seconds * 1000, now, 'en');
		expect(back(20)).toBe('now');
		expect(back(60)).toBe('1 minute ago');
		expect(back(5 * 60 + 59)).toBe('5 minutes ago');
		expect(back(6 * 3600)).toBe('6 hours ago');
		expect(back(30 * 3600)).toBe('yesterday');
		expect(back(3 * 86400)).toBe('3 days ago');
		expect(back(8 * 86400)).toBe('last week');
		expect(back(65 * 86400)).toBe('2 months ago');
		expect(back(400 * 86400)).toBe('last year');
		expect(ago(now - 6 * 3600 * 1000, now, 'fr')).toBe('il y a 6 heures');
		expect(stamp(now, 'fr')).toBe('9 oct. 2026, 14:37');
	});
});

describe('an answer again', () => {
	it('enters beside the answers of its message, from the very prefix, every branch kept', () => {
		const c: Conversation = { id: 'c', title: 't', updated: 0, entries: [], leaf: null };
		const one = append(c, null, { role: 'user', text: 'one' });
		const answer = append(c, one.id, { role: 'assistant', rounds: [] });
		const more = append(c, answer.id, { role: 'user', text: 'more' });
		c.leaf = one.id;
		const prefix = path(c);
		const again = append(c, one.id, { role: 'assistant', rounds: [] });
		expect(prefix).toEqual([one]);
		expect(forks(c).get(one.id)).toEqual([answer, again]);
		expect(path(c)).toEqual([one, again]);
		expect(latest(c, answer.id)).toBe(more.id);
	});
});

describe('a slash command closed', () => {
	it('leaves the tree, what follows it following its parent, versions and history kept', () => {
		const c: Conversation = { id: 'c', title: 't', updated: 0, entries: [], leaf: null };
		const one = append(c, null, { role: 'user', text: 'one' });
		const answer = append(c, one.id, { role: 'assistant', rounds: [] });
		const cli = append(c, answer.id, { role: 'cli', input: 'show title', output: 't', ok: true });
		const two = append(c, cli.id, { role: 'user', text: 'two' });
		const deux = append(c, cli.id, { role: 'user', text: 'deux' });
		const last = append(c, deux.id, { role: 'cli', input: 'show title', output: 't', ok: true });
		const model = path(c).filter((e) => e.role !== 'cli');
		drop(c, cli.id);
		expect(c.entries).toHaveLength(5);
		expect(forks(c).get(answer.id)).toEqual([two, deux]);
		expect(path(c).filter((e) => e.role !== 'cli')).toEqual(model);
		drop(c, last.id);
		expect(c.leaf).toBe(deux.id);
		expect(path(c)).toEqual(model);
	});
});

describe('a conversation file', () => {
	it('reads back to every conversation as it settled, ids and branches kept', () => {
		const c = demo();
		const edited = append(c, null, { role: 'user', text: 'bonjour' });
		const other: Conversation = { id: 'o', title: 'Other', updated: 1, entries: [], leaf: null };
		const back = parse(serialize([c, other]));
		expect(back.map((x) => x.id)).toEqual(['c', 'o']);
		expect(back[0].title).toBe('Demo');
		expect(back[0].updated).toBe(c.updated);
		expect(back[0].leaf).toBe(edited.id);
		expect(back[0].entries.map((e) => e.id)).toEqual(c.entries.map((e) => e.id));
		const assistant = back[0].entries[1];
		expect(assistant.role === 'assistant' && assistant.rounds[0].calls.map((x) => x.id)).toEqual([
			'a'
		]);
		expect(parse(serialize(back))).toEqual(back);
	});

	it('imports nothing when it does not read whole, and says where', () => {
		const file = JSON.parse(serialize([demo()]));
		const altered = (change: (f: typeof file) => void) => {
			const copy = structuredClone(file);
			change(copy);
			return () => parse(JSON.stringify(copy));
		};
		const at = 'file.conversations[0]';
		expect(() => parse('{')).toThrow('not JSON');
		expect(altered((f) => (f.kiss = 'save'))).toThrow('file.kiss: not conversations');
		expect(altered((f) => (f.extra = 1))).toThrow('file.extra: unknown');
		expect(altered((f) => delete f.conversations[0].title)).toThrow(`${at}.title: missing`);
		expect(altered((f) => delete f.conversations[0].entries[0].time)).toThrow(
			`${at}.entries[0].time: missing`
		);
		expect(altered((f) => f.conversations.push(f.conversations[0]))).toThrow(
			'file.conversations[1].id: used twice'
		);
		expect(altered((f) => (f.conversations[0].entries[0].role = 'system'))).toThrow(
			`${at}.entries[0].role: not one of user assistant cli`
		);
		expect(altered((f) => delete f.conversations[0].entries[1].rounds[0].calls[0].ok)).toThrow(
			`${at}.entries[1].rounds[0].calls[0].ok: missing`
		);
		expect(altered((f) => (f.conversations[0].entries[2].ok = 'yes'))).toThrow(
			`${at}.entries[2].ok: not a boolean`
		);
		const entries = (f: typeof file) => f.conversations[0].entries;
		expect(altered((f) => (entries(f)[1].id = entries(f)[0].id))).toThrow(
			`${at}.entries[1].id: used twice`
		);
		expect(altered((f) => (entries(f)[0].parent = entries(f)[2].id))).toThrow(
			`${at}.entries[0].parent: names no entry before it`
		);
		expect(altered((f) => (f.conversations[0].leaf = 'gone'))).toThrow(
			`${at}.leaf: names no entry`
		);
		expect(altered((f) => (f.conversations[0].leaf = entries(f)[0].id))).toThrow(
			`${at}.leaf: ends no branch`
		);
		expect(altered((f) => (f.conversations[0].leaf = null))).toThrow(`${at}.leaf: names no entry`);
	});

	it('adds only the conversations whose id is new', () => {
		const here: Conversation = { id: 'h', title: 'Here', updated: 2, entries: [], leaf: null };
		const other: Conversation = { id: 'o', title: 'Other', updated: 1, entries: [], leaf: null };
		expect(fresh([here, other], [here])).toEqual({ added: [other], skipped: [here] });
	});
});
