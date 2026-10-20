import { describe, expect, it } from 'vitest';
import type { Conversation, Message, Round } from '../src/lib/types.js';
import { compacted, folding } from '../src/lib/compaction.js';
import { parse, serialize, settled } from '../src/lib/conversation.js';
import chat from '../src/protocols/chat.js';
import prompts from '../src/prompts.json';

const round = (text: string): Round => ({ reasoning: '', text, calls: [] });
const summary = (text: string, kept: number): Round => ({ ...round(text), kept });
const framed = (text: string) => prompts.compaction.summary.replace('{summary}', text);

// what the model reads of a history, a step by its text, a summary framed
const read = (messages: readonly Message[]) =>
	messages.map((m) =>
		m.role === 'user' ? m.text : m.role === 'assistant' ? m.rounds.map((r) => r.text) : m.output
	);

// a history of two turns, the second one of three rounds, a command between
const history: Message[] = [
	{ role: 'user', text: 'one' },
	{ role: 'assistant', rounds: [round('a1')] },
	{ role: 'cli', input: 'show title', output: 'title x', ok: true },
	{ role: 'user', text: 'two' },
	{ role: 'assistant', rounds: [round('b1'), round('b2'), round('b3')] }
];

describe('a history the model reads', () => {
	it('is every step without a summary, the commands left out', () => {
		expect(read(compacted(history))).toEqual(['one', ['a1'], 'two', ['b1', 'b2', 'b3']]);
	});

	it('starts at the last summary, its kept steps first, the summary itself never sent', () => {
		const h: Message[] = [
			...history.slice(0, 4),
			{ role: 'assistant', rounds: [round('b1'), round('b2'), summary('S', 2), round('b3')] }
		];
		expect(read(compacted(h))).toEqual([framed('S'), ['b1', 'b2', 'b3']]);
	});

	it('reads the kept steps across the summary before it, which it folds', () => {
		const h: Message[] = [
			{ role: 'user', text: 'one' },
			{ role: 'assistant', rounds: [round('a1'), summary('S1', 1), round('a2')] },
			{ role: 'user', text: 'two' },
			{ role: 'assistant', rounds: [round('b1'), summary('S2', 3), round('b2')] }
		];
		expect(read(compacted(h))).toEqual([framed('S2'), ['a2'], 'two', ['b1', 'b2']]);
	});
});

describe('a summary asked', () => {
	it('folds the steps the model reads but the preserved ones, then asks', () => {
		const fold = folding(history, 2)!;
		expect(fold.kept).toBe(2);
		expect(read(fold.messages)).toEqual(['one', ['a1'], 'two', ['b1'], prompts.compaction.ask]);
	});

	it('sends the very prefix the model last read, so its endpoint reuses what it cached', () => {
		const h: Message[] = [
			...history.slice(0, 4),
			{ role: 'assistant', rounds: [round('b1'), summary('S', 1), round('b2'), round('b3')] }
		];
		const fold = folding(h, 1)!;
		expect(read(fold.messages)).toEqual([framed('S'), ['b1', 'b2'], prompts.compaction.ask]);
		const sent = (messages: readonly Message[]) =>
			(
				chat.body({ model: 'm', system: 's', messages, tools: [], parameters: {} }) as {
					messages: object[];
				}
			).messages;
		const asked = sent(fold.messages);
		expect(asked.slice(0, -1)).toEqual(sent(compacted(h)).slice(0, asked.length - 1));
	});

	it('folds nothing when no step lies past the preserved ones', () => {
		expect(folding(history, 5)).not.toBeNull();
		expect(folding(history, 6)).toBeNull();
		const h: Message[] = [{ role: 'assistant', rounds: [round('a1'), summary('S', 1)] }];
		expect(folding(h, 0)).not.toBeNull();
		expect(folding(h, 1)).toBeNull();
	});
});

describe('a summary kept', () => {
	it('holds once a round follows it and it wrote its text', () => {
		expect(settled([round('a'), summary('S', 1), round('b')]).map((r) => r.text)).toEqual([
			'a',
			'S',
			'b'
		]);
		expect(settled([round('a'), summary('S', 1)]).map((r) => r.text)).toEqual(['a']);
		expect(settled([round('a'), { ...summary('', 1), reasoning: 'r' }, round('b')])).toEqual([
			round('a'),
			round('b')
		]);
		expect(settled([round('a'), summary('S', 1), round('')])).toEqual([round('a')]);
	});

	it('reads back from a file, kept a count, and with it the history the model reads', () => {
		const c: Conversation = {
			id: 'c',
			title: 't',
			updated: 1,
			leaf: 'b',
			entries: [
				{ id: 'a', parent: null, time: 1, role: 'user', text: 'one' },
				{
					id: 'b',
					parent: 'a',
					time: 2,
					role: 'assistant',
					rounds: [round('a1'), summary('S', 0), round('a2')],
					stats: { tokens: 1, generation: 1, system: 1, context: 150 }
				}
			]
		};
		const [back] = parse(serialize([c]));
		expect(back).toEqual(c);
		expect(compacted(back.entries)).toEqual(compacted(c.entries));
		const wrong = serialize([c]).replace('"kept": 0', '"kept": 1.5');
		expect(() => parse(wrong)).toThrow(
			'file.conversations[0].entries[1].rounds[1].kept: not a count'
		);
	});
});
