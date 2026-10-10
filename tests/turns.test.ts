import { beforeEach, describe, expect, it, vi } from 'vitest';
import type { Assistant, Grant, ToolContext, Verdict } from '../src/lib/types.js';
import type { Pulse } from '../src/lib/pulse.js';
import { ALWAYS, ONCE, REFUSE } from '../src/lib/types.js';
import { app, edit, newChat, remove, send, stop } from '../src/lib/state.svelte.js';

// a turn of the model the test holds: it writes, asks and ends when told, and
// stops with its signal
interface Held {
	reply: Assistant;
	ctx: ToolContext;
	pulse: Pulse;
	end(): void;
}
const held = vi.hoisted(() => [] as Held[]);

vi.mock('../src/lib/agent.js', () => ({
	turn: (_: unknown, reply: Assistant, ctx: ToolContext, signal: AbortSignal, pulse: Pulse) =>
		new Promise<void>((end, fail) => {
			held.push({ reply, ctx, pulse, end });
			signal.addEventListener('abort', () => fail(signal.reason));
		})
}));

// the browser database stays out: the conversations live in the page alone
vi.mock('../src/lib/db.js', () => ({
	listConversations: async () => [],
	putConversations: async () => {},
	deleteConversations: async () => {}
}));

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

// the user answers the call shown
function answer(verdict: Verdict): void {
	const shown = app.asks[0];
	if (shown?.kind !== 'grant') throw new Error('no call shown');
	shown.settle(verdict);
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
		expect(last(first).error).toBe('stopped');
		held[1].end();
		await sent;
		expect(last(second).error).toBeUndefined();
	});

	it('ask in turn, each question answered to the turn that asked it', async () => {
		const { first, second, sent } = await two();
		const asked = [held[0].ctx.grant(call('x')), held[1].ctx.grant(call('y'))];
		expect(app.asks.map((q) => q.from)).toEqual([first, second]);
		answer(ONCE);
		expect(app.asks.map((q) => q.from)).toEqual([second]);
		answer(ALWAYS);
		expect(await Promise.all(asked)).toEqual<Verdict[]>([ONCE, ALWAYS]);
		held.forEach((h) => h.end());
		await sent;
	});

	it('answer no to the questions of a turn stopped, the others still standing', async () => {
		const { first, second, sent } = await two();
		const asked = [held[0].ctx.grant(call('x')), held[1].ctx.grant(call('y'))];
		stop(first);
		expect(await asked[0]).toBe(REFUSE);
		expect(app.asks.map((q) => q.from)).toEqual([second]);
		answer(ONCE);
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
		const { sent } = await two();
		const [a, b] = held.map((h) => [h.pulse.start, h.pulse.since]);
		let now = performance.now();
		const clock = vi.spyOn(performance, 'now').mockImplementation(() => now);
		const asked = held[0].ctx.grant(call('x'));
		now += 5000;
		answer(ONCE);
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
