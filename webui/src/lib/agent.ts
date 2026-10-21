import type { Assistant, Call, Message, Outcome, Request, Tool, ToolContext } from './types.js';
import type { Endpoint } from './api.js';
import { ALWAYS, REFUSE } from '../engine/engine.js';
import { chat, describe, pick } from './api.js';
import { PRESERVE, TRIGGER, compacted, folding } from './compaction.js';
import { aggregate, connecting } from './mcp.js';
import { settled } from './conversation.js';
import { MODEL_SEPARATOR } from './config.js';
import { chunk, close, mark, retry, stats, type Pulse } from './pulse.js';
import { CONSENT, OFF, ON, tools as own } from './tools.js';
import { PARAMETERS } from '../modules/models.js';
import { run, settings } from '../engine/run.js';

// whether the user lets the model make this call: a tool in consent asks, and
// always turns it on, as the user
async function allowed(ctx: ToolContext, c: Call): Promise<boolean> {
	if (settings.get('tools use', c.name) !== CONSENT) return true;
	const verdict = await ctx.grant({ kind: 'call', tool: c.name, args: c.args });
	ctx.signal.throwIfAborted();
	if (verdict === ALWAYS) await run(`set tools use ${JSON.stringify(c.name)} ${ON}`, 'user');
	return verdict !== REFUSE;
}

// the arguments of a call as an object, none when they read as anything else
function object(text: string): Record<string, unknown> | null {
	try {
		const value: unknown = text ? JSON.parse(text) : {};
		return value && typeof value === 'object' && !Array.isArray(value)
			? (value as Record<string, unknown>)
			: null;
	} catch {
		return null;
	}
}

// runs one call and writes its outcome into it; a call reaches a tool not off
// as the configuration stands when it goes, whatever the round offered; it
// takes its arguments as its tool keeps them, and is stored as sent before it
// reaches its tool, so a stop or a page closed while it runs leaves it
// stopped, whatever the tool answers
async function call(tools: readonly Tool[], ctx: ToolContext, c: Call): Promise<void> {
	const tool = tools.find((t) => t.name === c.name && settings.get('tools use', t.name) !== OFF);
	const args = object(c.args);
	if (!tool || !args) {
		// what no tool reads, or what does not read, is kept as no arguments
		c.args = '{}';
		c.result = tool ? 'The arguments are not a JSON object' : `Unknown tool "${c.name}"`;
		c.ok = false;
		return;
	}
	if (tool.stored) c.args = JSON.stringify(tool.stored(ctx, args));
	if (!(await allowed(ctx, c))) {
		c.result = 'The user refused the call';
		c.ok = false;
		return;
	}
	c.sent = true;
	await ctx.keep();
	const result: Outcome = await tool
		.run(ctx, args)
		.catch((e: Error) => ({ ok: false, text: e.message }));
	ctx.signal.throwIfAborted();
	if (result.images) c.images = result.images;
	c.result = result.text;
	c.ok = result.ok;
}

// what one round sends and may call, read from the running configuration as
// it stands: the model, its parameters, what its endpoint lists of it when
// the protocol reads it, the system prompt, the history as the model reads
// it, and the own tools of KiSS then those of every MCP server, the same from
// round to round while the configuration stays, a server that fails serving
// the tools it listed last; and when the history of the model compacts, the
// tokens that fold it, none never, and the steps a summary keeps; the pulse
// names the servers the round waits for
async function setup(
	messages: readonly Message[],
	signal: AbortSignal,
	p: Pulse,
	down: Map<string, string>
) {
	const { endpoint, model } = await pick(settings, signal);
	const waits = connecting(settings, down);
	if (waits.length) mark(p, 'preparing', `mcp ${waits.join(', ')}`, performance.now());
	const system = String(settings.get('chat system-prompt') ?? '');
	const item = endpoint.name + MODEL_SEPARATOR + model;
	// the parameters set for this model, numbers as numbers
	const parameters = Object.fromEntries(
		Object.entries(PARAMETERS).flatMap(([name, def]) => {
			const value = settings.get(`models ${name}`, item);
			return value === undefined ? [] : [[name, def.kind === 'number' ? Number(value) : value]];
		})
	);
	const { tools } = await aggregate(settings, own, down);
	const info = endpoint.protocol.described ? await describe(endpoint, model, signal) : undefined;
	const request: Request = {
		model,
		system,
		messages: compacted(messages),
		tools,
		parameters,
		info
	};
	const trigger = settings.get(`models ${TRIGGER}`, item);
	const preserve = Number(settings.get(`models ${PRESERVE}`, item) ?? 0);
	return {
		endpoint,
		request,
		tools,
		trigger: trigger === undefined ? null : Number(trigger),
		preserve
	};
}

// the tokens the last request of a history held once answered, as its turn
// kept them, none when it kept none
function held(messages: readonly Message[]): number | undefined {
	for (let i = messages.length - 1; i >= 0; i--) {
		const m = messages[i];
		if (m.role === 'assistant') return m.stats?.context;
	}
	return undefined;
}

// the summary the steps a request folds go into, streamed into a round of
// the reply that keeps the steps it does not fold; it runs no call, and one
// that writes no summary, or that the output limit cuts, fails the turn
async function summarize(
	endpoint: Endpoint,
	request: Request,
	reply: Assistant,
	kept: number,
	signal: AbortSignal,
	p: Pulse,
	retrying: (status: number, attempt: number, attempts: number) => void
): Promise<void> {
	mark(p, 'compacting', endpoint.name, performance.now());
	reply.rounds.push({ reasoning: '', text: '', calls: [], kept, folding: true });
	const round = reply.rounds[reply.rounds.length - 1];
	let usage: number | undefined;
	let cut = false;
	for await (const d of chat(endpoint, request, signal, retrying)) {
		const now = performance.now();
		if (d.usage !== undefined) usage = d.usage;
		if (d.cut) cut = true;
		chunk(p, !!(d.reasoning || d.content), now);
		if (d.reasoning) round.reasoning += d.reasoning;
		if (d.content) round.text += d.content;
		mark(p, 'compacting', endpoint.name, now);
	}
	close(p, usage);
	if (cut) throw new Error(`Endpoint ${endpoint.name} cut the summary at its output limit`);
	if (!round.text) throw new Error(`Endpoint ${endpoint.name} wrote no summary`);
	delete round.folding;
}

// one assistant turn: rounds streamed into reply as they arrive, until a round
// calls nothing; every round reads the configuration as it stands, so what a
// call changes holds from the next round on; a round first compacts the
// history once its last request held as many tokens as the trigger of its
// model, or once a batch of the model asked it to; a turn that compacts writes
// the summary alone, failing when no step lies past the preserved ones; reply
// belongs to the page state, so the round and its calls are read back from it
// once added; after a stop no call runs, and the reply keeps what settled
// before it; the pulse follows the turn chunk by chunk, and what it spent
// enters the reply once it streamed, with the tokens its last request held
export async function turn(
	messages: readonly Message[],
	reply: Assistant,
	ctx: ToolContext,
	signal: AbortSignal,
	p: Pulse,
	compact = false
): Promise<void> {
	let r = 0;
	// a compaction asked before the next request
	let asked = compact;
	// what the calls reach, a batch of the model asking for a compaction
	const own: ToolContext = {
		...ctx,
		cli: async (lines) => {
			const outcome = await ctx.cli(lines);
			if (outcome.compact) asked = true;
			return outcome;
		}
	};
	// the servers that failed this turn, as they were named, with what they
	// answered
	const down = new Map<string, string>();
	// the tokens the last request held once answered, none once unknown
	let context = held(messages);
	try {
		for (; r < Number(settings.get('tools max-rounds')); r++) {
			p.round = r + 1;
			mark(p, 'preparing', '', performance.now());
			// the history as it stands before this round, which the round goes on
			// writing while the request streams
			const history = [...messages, { ...reply, rounds: [...reply.rounds] }];
			const { endpoint, request, tools, trigger, preserve } = await setup(history, signal, p, down);
			const retrying = (status: number, attempt: number, attempts: number) =>
				retry(p, endpoint.name, status, attempt, attempts, performance.now());
			const fold =
				asked || (trigger !== null && context !== undefined && context >= trigger)
					? folding(history, preserve)
					: null;
			asked = false;
			if (compact && !fold) throw new Error('Nothing to compact');
			if (fold) {
				await summarize(
					endpoint,
					{ ...request, messages: fold.messages },
					reply,
					fold.kept,
					signal,
					p,
					retrying
				);
				request.messages = compacted([...messages, { ...reply, rounds: [...reply.rounds] }]);
			}
			context = undefined;
			if (compact) return;
			mark(p, 'waiting', endpoint.name, performance.now());
			reply.rounds.push({ reasoning: '', text: '', calls: [] });
			const round = reply.rounds[reply.rounds.length - 1];
			// the calls of the round by the index the stream gives them
			const calls = new Map<number, Call>();
			let usage: number | undefined;
			for await (const d of chat(endpoint, request, signal, retrying)) {
				const now = performance.now();
				if (d.usage !== undefined) usage = d.usage;
				if (d.context !== undefined) context = d.context;
				chunk(p, !!(d.reasoning || d.content || d.calls), now);
				if (d.reasoning) {
					round.reasoning += d.reasoning;
					mark(p, 'thinking', '', now);
				}
				if (d.content) {
					round.text += d.content;
					mark(p, 'writing', '', now);
				}
				if (d.opaque) {
					round.opaque ??= { protocol: endpoint.protocol.name, items: [] };
					round.opaque.items.push(d.opaque);
				}
				for (const c of d.calls ?? []) {
					if (!calls.has(c.index)) {
						round.calls.push({ id: c.id ?? `call-${r}-${c.index}`, name: '', args: '' });
						calls.set(c.index, round.calls[round.calls.length - 1]);
					}
					const part = calls.get(c.index)!;
					if (c.id) part.id = c.id;
					if (c.name) part.name += c.name;
					if (c.args) part.args += c.args;
					mark(p, 'calling', part.name, now);
				}
			}
			close(p, usage);
			if (!round.calls.length) return;
			for (const c of round.calls) {
				signal.throwIfAborted();
				mark(p, 'running', c.name, performance.now());
				await call(tools, own, c);
			}
		}
		reply.error = `Stopped after ${r} tool rounds`;
	} finally {
		reply.rounds = settled(reply.rounds);
		const spent = stats(p, performance.now());
		if (spent) reply.stats = context === undefined ? spent : { ...spent, context };
	}
}
