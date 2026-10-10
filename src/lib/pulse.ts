import type { Stats } from './types.js';

// what a turn does now, as the system spends it: waiting for the endpoint,
// streaming its thinking, its text or a call, or running a tool
export type Phase = 'waiting' | 'thinking' | 'writing' | 'calling' | 'running';

// the clock of one turn, in milliseconds of performance.now(); the time the
// user takes on a question is taken out of it, so it measures the system alone
export interface Pulse {
	// the round streamed now, from 1
	round: number;
	phase: Phase;
	// the endpoint waited for, or the tool called or run
	name: string;
	// when the phase began
	since: number;
	// when the turn began
	start: number;
	// the first chunk of the round and the last of the turn, NaN before them
	first: number;
	last: number;
	// the tokens of the round, one per chunk until the endpoint counts them
	tokens: number;
	// the tokens and the time of generation of the rounds ended
	total: number;
	generation: number;
}

export function pulse(now: number): Pulse {
	return {
		round: 1,
		phase: 'waiting',
		name: '',
		since: now,
		start: now,
		first: NaN,
		last: NaN,
		tokens: 0,
		total: 0,
		generation: 0
	};
}

// the phase the turn enters, its clock restarting only when it changes
export function mark(p: Pulse, phase: Phase, name: string, now: number): void {
	if (p.phase === phase && p.name === name) return;
	p.phase = phase;
	p.name = name;
	p.since = now;
}

// a chunk of the stream, holding a token or not
export function chunk(p: Pulse, token: boolean, now: number): void {
	if (Number.isNaN(p.first)) p.first = now;
	p.last = now;
	if (token) p.tokens++;
}

// the round ends, its tokens counted by the endpoint when it gives them; a
// round ended twice counts once
export function close(p: Pulse, usage?: number): void {
	if (Number.isNaN(p.first)) return;
	p.total += usage ?? p.tokens;
	p.generation += p.last - p.first;
	p.first = NaN;
	p.tokens = 0;
}

// the time the user took on a question, out of every clock of the turn
export function held(p: Pulse, ms: number): void {
	p.since += ms;
	p.start += ms;
}

// what the turn spent, once it streamed anything
export function stats(p: Pulse, now: number): Stats | undefined {
	close(p);
	if (Number.isNaN(p.last)) return undefined;
	return {
		tokens: p.total,
		generation: Math.round(p.generation),
		system: Math.round(now - p.start)
	};
}

const number = (n: number): string => Math.round(n).toLocaleString('en-US');
const ms = (n: number): string => `${number(n)} ms`;
const tokens = (n: number): string => `${number(n)} ${n === 1 ? 'token' : 'tokens'}`;
const rate = (n: number, time: number): string => `${((n * 1000) / time).toFixed(1)} t/s`;

// what each phase reads as, before the name it holds
const PHASES: Record<Phase, string> = {
	waiting: 'waiting for',
	thinking: 'thinking',
	writing: 'writing',
	calling: 'calling',
	running: 'running'
};

// the line of a turn at a moment: its round from the second on, the phase and
// its clock, and while it streams its tokens and their rate, from its second
// token, between its first chunk and its last
export function line(p: Pulse, now: number): string {
	const parts = p.round > 1 ? [`round ${p.round}`] : [];
	parts.push(p.name ? `${PHASES[p.phase]} ${p.name}` : p.phase, ms(now - p.since));
	if (p.phase === 'thinking' || p.phase === 'writing') {
		parts.push(tokens(p.tokens));
		if (p.tokens > 1) parts.push(rate(p.tokens - 1, p.last - p.first));
	}
	return parts.join(' - ');
}

// what a turn spent, as it reads under its answer
export function summary(s: Stats): string {
	const parts = [tokens(s.tokens)];
	if (s.generation > 0) parts.push(rate(s.tokens, s.generation));
	parts.push(ms(s.system));
	return parts.join(' - ');
}
