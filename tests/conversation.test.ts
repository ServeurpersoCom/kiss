import { describe, expect, it } from 'vitest';
import type { Conversation } from '../src/lib/types.js';
import { parse, serialize, source } from '../src/lib/conversation.js';

const conversation: Conversation = {
	id: 'c',
	title: 'Demo',
	updated: 1760000000000,
	messages: [
		{ role: 'user', text: 'translate hi' },
		{
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
		},
		{ role: 'cli', input: 'show title', output: 'title Demo', ok: true }
	]
};

describe('a message copied', () => {
	it('gives its source: the text typed, the answer without thinking or calls, the output', () => {
		const [user, assistant, cli] = conversation.messages;
		expect(source(user)).toBe('translate hi');
		expect(source(assistant)).toBe('first\n\nsalut');
		expect(source(cli)).toBe('title Demo');
	});
});

describe('a conversation file', () => {
	it('reads back to the conversation as it settled, under a new id', () => {
		const back = parse(serialize(conversation));
		expect(back).not.toHaveProperty('id');
		expect(back.title).toBe('Demo');
		expect(back.updated).toBe(conversation.updated);
		const assistant = back.messages[1];
		expect(assistant.role === 'assistant' && assistant.rounds[0].calls.map((c) => c.id)).toEqual([
			'a'
		]);
		expect(parse(serialize({ id: 'd', ...back }))).toEqual(back);
	});

	it('imports nothing when it does not read whole, and says where', () => {
		const file = JSON.parse(serialize(conversation));
		const altered = (change: (f: typeof file) => void) => {
			const copy = structuredClone(file);
			change(copy);
			return () => parse(JSON.stringify(copy));
		};
		expect(() => parse('{')).toThrow('not JSON');
		expect(altered((f) => (f.kiss = 'save'))).toThrow('file.kiss: not conversation');
		expect(altered((f) => delete f.title)).toThrow('file.title: missing');
		expect(altered((f) => (f.extra = 1))).toThrow('file.extra: unknown');
		expect(altered((f) => (f.messages[0].role = 'system'))).toThrow(
			'file.messages[0].role: not one of user assistant cli'
		);
		expect(altered((f) => delete f.messages[1].rounds[0].calls[0].ok)).toThrow(
			'file.messages[1].rounds[0].calls[0].ok: missing'
		);
		expect(altered((f) => (f.messages[2].ok = 'yes'))).toThrow(
			'file.messages[2].ok: not a boolean'
		);
	});
});
