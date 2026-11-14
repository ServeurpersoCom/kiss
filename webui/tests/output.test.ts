import { describe, expect, it } from 'vitest';
import { Lines, OUTPUT_SHOWN, bounded } from '../src/lib/output.js';

describe('the output of a call', () => {
	it('reaches the model whole within its budget, else its head and its tail, as the server cuts it', () => {
		expect(bounded('one\ntwo\n', 64)).toBe('one\ntwo\n');
		expect(bounded('one\ntwo\nsix\nten\n', 10)).toBe(
			'one\nLong output with 8 bytes hidden from context\nten\n'
		);
		expect(bounded('config', 2)).toBe('c\nLong output with 4 bytes hidden from context\ng');
		// a cut inside a character reads as one replacement character
		expect(bounded('\u00e9\u00e9\u00e9', 3)).toBe(
			'\uFFFD\nLong output with 3 bytes hidden from context\n\u00e9'
		);
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
