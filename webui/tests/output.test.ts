import { describe, expect, it } from 'vitest';
import { Lines, OUTPUT_SHOWN, bounded } from '../src/lib/output.js';

describe('the output of a call', () => {
	it('reaches the model within its budget, head, hidden bytes and tail, a cut one staying as it is, as the server cuts it', () => {
		const lines = Array.from(
			{ length: 20 },
			(_, i) => `line${String(i + 1).padStart(2, '0')}\n`
		).join('');
		const cut = bounded(lines, 120);
		expect(bounded('one\ntwo\n', 64)).toBe('one\ntwo\n');
		expect(cut).toBe(
			'line01\nline02\nLong output with 70 bytes hidden from context\nline13\nline14\nline15\nline16\nline17\nline18\nline19\nline20\n'
		);
		expect(new TextEncoder().encode(cut).length).toBeLessThanOrEqual(120);
		expect(bounded(cut, 120)).toBe(cut);
	});

	it('shows its whole lines as they come, up to its bound, then the line that says so', () => {
		const open = new Lines(64);
		expect(open.add('on')).toBe('');
		expect(open.add('e\ntw')).toBe('one\n');
		expect(open.add('o\n')).toBe('two\n');
		const bound = new Lines(6);
		expect(bound.add('one\ntwo\n')).toBe('one\n' + OUTPUT_SHOWN);
		expect(bound.add('six\n')).toBe('');
		expect(new Lines(3).add('abcd')).toBe(OUTPUT_SHOWN);
	});
});
