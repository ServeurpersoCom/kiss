import { describe, expect, it } from 'vitest';
import type { Conversation, Entry } from '../src/lib/types.js';
import { append, forks, latest, parse, path, serialize, source } from '../src/lib/conversation.js';

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

describe('a conversation file', () => {
	it('reads back to the tree as it settled, under a new id', () => {
		const c = demo();
		const edited = append(c, null, { role: 'user', text: 'bonjour' });
		const back = parse(serialize(c));
		expect(back).not.toHaveProperty('id');
		expect(back.title).toBe('Demo');
		expect(back.updated).toBe(c.updated);
		expect(back.leaf).toBe(edited.id);
		expect(back.entries.map((e) => e.id)).toEqual(c.entries.map((e) => e.id));
		const assistant = back.entries[1];
		expect(assistant.role === 'assistant' && assistant.rounds[0].calls.map((x) => x.id)).toEqual([
			'a'
		]);
		expect(parse(serialize({ id: 'd', ...back }))).toEqual(back);
	});

	it('imports nothing when it does not read whole, and says where', () => {
		const file = JSON.parse(serialize(demo()));
		const altered = (change: (f: typeof file) => void) => {
			const copy = structuredClone(file);
			change(copy);
			return () => parse(JSON.stringify(copy));
		};
		expect(() => parse('{')).toThrow('not JSON');
		expect(altered((f) => (f.kiss = 'save'))).toThrow('file.kiss: not conversation');
		expect(altered((f) => delete f.title)).toThrow('file.title: missing');
		expect(altered((f) => (f.extra = 1))).toThrow('file.extra: unknown');
		expect(altered((f) => (f.entries[0].role = 'system'))).toThrow(
			'file.entries[0].role: not one of user assistant cli'
		);
		expect(altered((f) => delete f.entries[1].rounds[0].calls[0].ok)).toThrow(
			'file.entries[1].rounds[0].calls[0].ok: missing'
		);
		expect(altered((f) => (f.entries[2].ok = 'yes'))).toThrow('file.entries[2].ok: not a boolean');
		expect(altered((f) => (f.entries[1].id = f.entries[0].id))).toThrow(
			'file.entries[1].id: used twice'
		);
		expect(altered((f) => (f.entries[0].parent = f.entries[2].id))).toThrow(
			'file.entries[0].parent: names no entry before it'
		);
		expect(altered((f) => (f.leaf = 'gone'))).toThrow('file.leaf: names no entry');
		expect(altered((f) => (f.leaf = f.entries[0].id))).toThrow('file.leaf: ends no branch');
		expect(altered((f) => (f.leaf = null))).toThrow('file.leaf: names no entry');
	});
});
