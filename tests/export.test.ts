import { describe, expect, it } from 'vitest';
import { markdown } from '../src/lib/export.js';

describe('a conversation exported', () => {
	it('reads as the thread shows it, every code block fenced longer than its content', () => {
		const text = markdown({
			id: 'c',
			title: 'Demo',
			updated: 0,
			messages: [
				{ role: 'user', text: 'hi' },
				{
					role: 'assistant',
					rounds: [
						{
							reasoning: 'think',
							text: 'calling',
							calls: [{ id: 'a', name: 'bash', args: '{"c":"ls"}', result: 'a ``` b', ok: false }]
						}
					],
					error: 'stopped'
				},
				{ role: 'cli', input: 'show version', output: 'KiSS x', ok: true }
			]
		});
		expect(text).toBe(
			[
				'# Demo',
				'## User\n\nhi',
				'## Assistant',
				'<details><summary>Thinking</summary>\n\nthink\n\n</details>',
				'calling',
				'<details><summary>bash (failed)</summary>\n\n```json\n{"c":"ls"}\n```\n\n````\na ``` b\n````\n\n</details>',
				'**stopped**',
				'```\nKiSS# show version\nKiSS x\n```'
			].join('\n\n') + '\n'
		);
	});
});
