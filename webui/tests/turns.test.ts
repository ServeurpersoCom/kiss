import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Assistant, ToolContext } from '../src/lib/types.js';
import type { Grant, Verdict } from '../src/engine/engine.js';
import type { Pulse } from '../src/lib/pulse.js';
import { ALWAYS, ONCE, REFUSE } from '../src/engine/engine.js';
import { app, askOf, dismiss, edit, newChat, remove, send, stop } from '../src/lib/state.svelte.js';
import { NEW_DRAFT, draftOf, keep } from '../src/lib/drafts.js';

// a turn of the model the test holds: it writes, asks, ends or fails when
// told, and stops with its signal
interface Held {
	reply: Assistant;
	ctx: ToolContext;
	pulse: Pulse;
	end(): void;
	fail(error: Error): void;
}
const held = vi.hoisted(() => [] as Held[]);

vi.mock('../src/lib/agent.js', () => ({
	turn: (_: unknown, reply: Assistant, ctx: ToolContext, signal: AbortSignal, pulse: Pulse) =>
		new Promise<void>((end, fail) => {
			held.push({ reply, ctx, pulse, end, fail });
			signal.addEventListener('abort', () => fail(signal.reason));
		})
}));

// the browser database stays out: the conversations live in the page alone
vi.mock('../src/lib/db.js', () => ({
	listConversations: async () => [],
	putConversations: async () => {},
	deleteConversations: async () => {}
}));

// a random UUID, version 4
const UUID = /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/;

const call = (tool: string): Grant => ({ kind: 'call', tool, args: '{}' });

// two conversations, each with a turn the model writes now
async function two() {
	newChat();
	const a = send('one');
	const first = app.current!.id;
	newChat();
	const b = send('two');
	const second = app.current!.id;
	await vi.waitFor(() => expect(held).toHaveLength(2));
	return { first, second, sent: Promise.all([a, b]) };
}

// the call a conversation shows
function shown(id: string): Grant {
	const ask = askOf(id);
	if (ask?.kind !== 'grant') throw new Error('no call shown');
	return ask.request;
}

// the user answers the call a conversation shows
function answer(id: string, verdict: Verdict): void {
	const ask = askOf(id);
	if (ask?.kind !== 'grant') throw new Error('no call shown');
	ask.settle(verdict);
}

// the answer a conversation ends on
const last = (id: string) => {
	const c = app.conversations.find((c) => c.id === id)!;
	return c.entries.find((e) => e.id === c.leaf) as Assistant;
};

beforeEach(() => {
	held.length = 0;
	app.conversations = [];
	app.asks = [];
});

describe('a page served over plain HTTP', () => {
	it('names its conversations and entries without randomUUID, which it lacks', async () => {
		const random = globalThis.crypto;
		vi.stubGlobal('crypto', { getRandomValues: (a: Uint8Array) => random.getRandomValues(a) });
		try {
			newChat();
			const sent = send('one');
			await vi.waitFor(() => expect(held).toHaveLength(1));
			held[0].end();
			await sent;
		} finally {
			vi.unstubAllGlobals();
		}
		const ids = [app.current!.id, ...app.current!.entries.map((e) => e.id)];
		for (const id of ids) expect(id).toMatch(UUID);
		expect(new Set(ids).size).toBe(3);
	});
});

describe('turns in several conversations', () => {
	it('run at once, each in its own conversation, ending apart', async () => {
		const { first, second, sent } = await two();
		expect(Object.keys(app.replies)).toEqual([first, second]);
		held[0].reply.rounds.push({ reasoning: '', text: 'a', calls: [] });
		held[1].reply.rounds.push({ reasoning: '', text: 'b', calls: [] });
		held[1].end();
		await vi.waitFor(() => expect(Object.keys(app.replies)).toEqual([first]));
		held[0].end();
		await sent;
		expect(app.replies).toEqual({});
		expect([last(first).rounds[0].text, last(second).rounds[0].text]).toEqual(['a', 'b']);
	});

	it('stop one by one, the others writing on', async () => {
		const { first, second, sent } = await two();
		stop(first);
		await vi.waitFor(() => expect(Object.keys(app.replies)).toEqual([second]));
		expect(last(first).error).toBe('Stopped');
		held[1].end();
		await sent;
		expect(last(second).error).toBeUndefined();
	});

	it('ask each in its own conversation, answered in any order to the turn that asked', async () => {
		const { first, second, sent } = await two();
		const asked = [held[0].ctx.grant(call('x')), held[1].ctx.grant(call('y'))];
		expect([askOf(first)?.from, askOf(second)?.from]).toEqual([first, second]);
		answer(second, ALWAYS);
		expect(askOf(second)).toBeUndefined();
		expect(askOf(first)?.from).toBe(first);
		answer(first, ONCE);
		expect(await Promise.all(asked)).toEqual<Verdict[]>([ONCE, ALWAYS]);
		held.forEach((h) => h.end());
		await sent;
	});

	it('show their questions one after the other within a conversation', async () => {
		const { first, sent } = await two();
		const asked = [held[0].ctx.grant(call('x')), held[0].ctx.grant(call('y'))];
		expect(shown(first)).toEqual(call('x'));
		answer(first, ONCE);
		expect(shown(first)).toEqual(call('y'));
		answer(first, REFUSE);
		expect(await Promise.all(asked)).toEqual<Verdict[]>([ONCE, REFUSE]);
		held.forEach((h) => h.end());
		await sent;
	});

	it('answer no to the questions of a turn stopped, the others still standing', async () => {
		const { first, second, sent } = await two();
		const asked = [held[0].ctx.grant(call('x')), held[1].ctx.grant(call('y'))];
		stop(first);
		expect(await asked[0]).toBe(REFUSE);
		expect(app.asks.map((q) => q.from)).toEqual([second]);
		answer(second, ONCE);
		expect(await asked[1]).toBe(ONCE);
		held[1].end();
		await sent;
	});

	it('stop with their conversation deleted, the others writing on', async () => {
		const { first, second, sent } = await two();
		await remove([second]);
		await vi.waitFor(() => expect(Object.keys(app.replies)).toEqual([first]));
		held[0].end();
		await sent;
		expect(last(first).error).toBeUndefined();
	});

	it('leave the time the user takes on a question out of the clock of the turn that asks', async () => {
		const { first, sent } = await two();
		const [a, b] = held.map((h) => [h.pulse.start, h.pulse.since]);
		let now = performance.now();
		const clock = vi.spyOn(performance, 'now').mockImplementation(() => now);
		const asked = held[0].ctx.grant(call('x'));
		now += 5000;
		answer(first, ONCE);
		await asked;
		clock.mockRestore();
		expect([held[0].pulse.start, held[0].pulse.since]).toEqual(a.map((t) => t + 5000));
		expect([held[1].pulse.start, held[1].pulse.since]).toEqual(b);
		held.forEach((h) => h.end());
		await sent;
	});
});

describe('an edit', () => {
	it('enters a new version only when the text changes', async () => {
		newChat();
		const sent = send('one');
		await vi.waitFor(() => expect(held).toHaveLength(1));
		held[0].end();
		await sent;
		const entries = app.current!.entries.length;
		const user = app.current!.entries.find((e) => e.role === 'user')!;
		await edit(user.id, 'one');
		expect(held).toHaveLength(1);
		expect(app.current!.entries).toHaveLength(entries);
		const edited = edit(user.id, 'two');
		await vi.waitFor(() => expect(held).toHaveLength(2));
		held[1].end();
		await edited;
	});
});

describe('the error of a turn', () => {
	it('opens with a capital, but on a URL written as it is', async () => {
		for (const [message, shown] of [
			['chat model is not set -> /show models', 'Chat model is not set -> /show models'],
			['http://m/v1 does not answer', 'http://m/v1 does not answer']
		]) {
			newChat();
			const sent = send('go');
			await vi.waitFor(() => expect(held).toHaveLength(1));
			const id = app.current!.id;
			held[0].fail(new Error(message));
			await sent;
			expect(last(id).error).toBe(shown);
			held.length = 0;
		}
	});
});

describe('a slash command during a turn', () => {
	it('closes while the model answers, the answer ending whole in its conversation', async () => {
		newChat();
		const sent = send('one');
		await vi.waitFor(() => expect(held).toHaveLength(1));
		const id = app.current!.id;
		await send('/show version');
		const cli = app.current!.entries.find((e) => e.role === 'cli')!;
		await dismiss(cli.id);
		expect(app.current!.entries.some((e) => e.role === 'cli')).toBe(false);
		held[0].reply.rounds.push({ reasoning: '', text: 'done', calls: [] });
		held[0].end();
		await sent;
		expect(last(id).rounds[0].text).toBe('done');
	});
});

describe('a draft', () => {
	it('belongs to its conversation and goes with it, the one of a new chat staying', async () => {
		const { first, second, sent } = await two();
		keep(first, 'half typed');
		keep(second, 'other');
		keep(NEW_DRAFT, 'not sent yet');
		expect(draftOf(first)).toBe('half typed');
		await remove([second]);
		expect([draftOf(first), draftOf(second), draftOf(NEW_DRAFT)]).toEqual([
			'half typed',
			'',
			'not sent yet'
		]);
		keep(first, '');
		expect(draftOf(first)).toBe('');
		held[0].end();
		await sent;
	});
});
