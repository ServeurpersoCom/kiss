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

// a text as a tool returns it within a budget: whole when it fits, else its
// head and its tail, each half of the budget and cut on a line end when it
// holds one, the line that counts the bytes hidden between them
export function bounded(t: string, budget: number): string {
	const bytes = ENCODER.encode(t);
	const headMax = Math.floor(budget / 2);
	if (bytes.length <= budget) return t;
	const head = bytes.subarray(0, headMax);
	const last = bytes.subarray(bytes.length - (budget - headMax));
	const end = head.lastIndexOf(LINE_END);
	const begin = last.indexOf(LINE_END);
	const first = end < 0 ? text(head) + NEWLINE : text(head.subarray(0, end + 1));
	const final = begin < 0 ? last : last.subarray(begin + 1);
	const hidden = bytes.length - (end < 0 ? head.length : end + 1) - final.length;
	return `${first}Long output with ${hidden} bytes hidden from context\n${text(final)}`;
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
