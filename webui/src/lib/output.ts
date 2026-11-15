// output.ts: the text of a call as the model reads it, within a budget, and
// the output of a call as it shows while it runs, twins of lib/output.cpp of
// the server, bytes counted as UTF-8

import { NEWLINE } from './config.js';

const ENCODER = new TextEncoder();
const DECODER = new TextDecoder();

// the byte a line ends with
const LINE_END = 0x0a;

// the line that ends what an output shows while it runs once it shows its
// bound, the rest showing once the call ends
export const OUTPUT_SHOWN = 'The rest of the output shows once the call ends\n';

// bytes cut anywhere as text: a sequence a cut split reads as one U+FFFD at
// either end, as the server reads a run of bytes that is not UTF-8
function text(bytes: Uint8Array): string {
	let start = 0;
	while (start < bytes.length && (bytes[start] & 0xc0) === 0x80) start++;
	return (start ? '\uFFFD' : '') + DECODER.decode(bytes.subarray(start));
}

// the line that counts the bytes a cut hides
const hidden = (n: number): string => `Long output with ${n} bytes hidden from context\n`;

// the whole lines at the front of bytes that hold n bytes or more, taken off
// them, all of them when no line ends there
function dropFront(bytes: Uint8Array, n: number): Uint8Array {
	const at = bytes.indexOf(LINE_END, n - 1);
	return at < 0 ? bytes.subarray(bytes.length) : bytes.subarray(at + 1);
}

// the whole lines at the back of bytes that hold n bytes or more, taken off them
function dropBack(bytes: Uint8Array, n: number): Uint8Array {
	const room = Math.max(bytes.length - n, 0);
	const at = room === 0 ? -1 : bytes.lastIndexOf(LINE_END, room - 1);
	return bytes.subarray(0, at + 1);
}

// a text as a tool returns it within a budget: whole when it fits, else its
// head, the line that counts the bytes hidden, and its tail, the three within
// the budget, so a text cut already comes back as it is
export function bounded(t: string, budget: number): string {
	const bytes = ENCODER.encode(t);
	const headMax = Math.floor(budget / 2);
	if (bytes.length <= budget) return t;
	const head = bytes.subarray(0, headMax);
	const last = bytes.subarray(bytes.length - (budget - headMax));
	// both cuts fall on line ends when the kept bytes hold one, so no line and
	// no UTF-8 sequence comes out split
	const end = head.lastIndexOf(LINE_END);
	const begin = last.indexOf(LINE_END);
	let first: Uint8Array = ENCODER.encode(
		end < 0 ? text(head) + NEWLINE : text(head.subarray(0, end + 1))
	);
	let final: Uint8Array = ENCODER.encode(text(begin < 0 ? last : last.subarray(begin + 1)));
	// the bytes of the text the head and the tail still show
	let shownFirst = end < 0 ? head.length : end + 1;
	let shownFinal = begin < 0 ? last.length : last.length - begin - 1;
	// the head, the line and the tail fit the budget together: whole lines
	// go from the larger of the head and the tail, the head on a tie, so the
	// end of the text, and the line that closes it, stays
	for (;;) {
		const line = hidden(bytes.length - shownFirst - shownFinal);
		const size = first.length + ENCODER.encode(line).length + final.length;
		if (size <= budget || (!first.length && !final.length))
			return DECODER.decode(first) + line + DECODER.decode(final);
		if (first.length && first.length >= final.length) {
			const kept = dropBack(first, size - budget);
			shownFirst = kept.length ? shownFirst - Math.min(first.length - kept.length, shownFirst) : 0;
			first = kept;
		} else {
			const kept = dropFront(final, size - budget);
			shownFinal = kept.length ? shownFinal - Math.min(final.length - kept.length, shownFinal) : 0;
			final = kept;
		}
	}
}

// the whole lines of an output as they come, for whoever shows it while it
// runs: at most max bytes of them, then the line OUTPUT_SHOWN, then nothing
export class Lines {
	// the text after the last line given
	private pending = '';
	private shown = 0;
	private full = false;

	constructor(private readonly max: number) {}

	// the lines the text completes, empty when none
	add(t: string): string {
		if (this.full) return '';
		const bytes = ENCODER.encode(this.pending + t);
		const room = this.max - this.shown;
		const end = bytes.lastIndexOf(LINE_END);
		// the whole lines that fit, the line that ends the show when the rest
		// does not: a line past the room, or one still open that already fills it
		const fits =
			end < 0 ? -1 : end < room ? end : room === 0 ? -1 : bytes.lastIndexOf(LINE_END, room - 1);
		let out = fits < 0 ? '' : DECODER.decode(bytes.subarray(0, fits + 1));
		this.shown += fits + 1;
		const rest = bytes.subarray(fits + 1);
		this.pending = DECODER.decode(rest);
		if ((end >= 0 && fits !== end) || rest.length > this.max - this.shown) {
			this.full = true;
			this.pending = '';
			out += OUTPUT_SHOWN;
		}
		return out;
	}
}
