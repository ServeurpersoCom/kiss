import { describe, expect, it } from 'vitest';
import { chunk, close, held, line, mark, pulse, stats, summary } from '../src/lib/pulse.js';

describe('the line of a turn', () => {
	it('prepares, then waits for the endpoint, its clock from the request on', () => {
		const p = pulse(0);
		expect(line(p, 3)).toBe('Preparing - 3 ms');
		mark(p, 'waiting', 'pod', 10);
		expect(line(p, 852)).toBe('Waiting for pod - 842 ms');
	});

	it('streams its tokens, their rate from the second one, between first and last chunk', () => {
		const p = pulse(0);
		mark(p, 'waiting', 'pod', 0);
		chunk(p, false, 100);
		expect(line(p, 150)).toBe('Waiting for pod - 150 ms');
		chunk(p, true, 100);
		mark(p, 'thinking', '', 100);
		expect(line(p, 100)).toBe('Thinking - 0 ms - 1 token');
		chunk(p, true, 120);
		chunk(p, true, 140);
		mark(p, 'writing', '', 140);
		expect(line(p, 1340)).toBe('Writing - 1,200 ms - 3 tokens - 50.0 t/s');
	});

	it('names the call written and the tool run, and the round from the second on', () => {
		const p = pulse(0);
		mark(p, 'calling', 'config', 5);
		expect(line(p, 317)).toBe('Calling config - 312 ms');
		p.round = 2;
		mark(p, 'running', 'config', 400);
		expect(line(p, 447)).toBe('Round 2 - Running config - 47 ms');
	});

	it('restarts its clock only when the phase or its name changes', () => {
		const p = pulse(0);
		mark(p, 'writing', '', 10);
		mark(p, 'writing', '', 50);
		expect(line(p, 60)).toBe('Writing - 50 ms - 0 tokens');
	});
});

describe('what a turn spent', () => {
	it('counts the tokens the endpoint gives, else one per chunk, each round once', () => {
		const p = pulse(0);
		chunk(p, true, 40);
		chunk(p, true, 240);
		close(p, 9);
		close(p, 9);
		chunk(p, true, 300);
		chunk(p, true, 400);
		expect(stats(p, 1000)).toEqual({ tokens: 11, generation: 300, system: 1000 });
	});

	it('leaves the time the user takes on a question out of every clock', () => {
		const p = pulse(0);
		chunk(p, true, 100);
		close(p);
		mark(p, 'running', 'bash', 200);
		held(p, 5000);
		expect(line(p, 5250)).toBe('Running bash - 50 ms');
		expect(stats(p, 5300)!.system).toBe(300);
	});

	it('is nothing before a first chunk', () => {
		expect(stats(pulse(0), 500)).toBeUndefined();
	});

	it('reads with its rate when it generated', () => {
		const s = { tokens: 1204, generation: 23470, system: 23512 };
		expect(summary(s)).toBe('1,204 tokens - 51.3 t/s - 23,512 ms');
		expect(summary({ tokens: 1, generation: 0, system: 95 })).toBe('1 token - 95 ms');
	});
});
