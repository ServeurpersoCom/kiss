import type { Assistant, Call, Message, Outcome, Request, Tool, ToolContext } from './types.js';
import { ALWAYS, REFUSE } from './types.js';
import { chat, pick } from './api.js';
import { aggregate, connecting } from './mcp.js';
import prompts from './prompts.json';
import { settled } from './conversation.js';
import { MODEL_SEPARATOR } from './config.js';
import { chunk, close, mark, stats, type Pulse } from './pulse.js';
import { CONSENT, OFF, ON, tools as own } from './tools.js';
import models from '../modules/models.js';
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
// takes its arguments as its tool keeps them, and is stored as sent before it reaches its tool, so a
// stop or a page closed while it runs leaves it stopped, whatever the tool
// answers
async function call(tools: readonly Tool[], ctx: ToolContext, c: Call): Promise<void> {
	const tool = tools.find((t) => t.name === c.name && settings.get('tools use', t.name) !== OFF);
	const args = object(c.args);
	if (!tool || !args) {
		// what no tool reads, or what does not read, cannot be masked, so none
		// of it is kept
		c.args = '{}';
		c.result = tool ? 'the arguments are not a JSON object' : `unknown tool "${c.name}"`;
		c.ok = false;
		return;
	}
	if (tool.stored) c.args = JSON.stringify(tool.stored(ctx, args));
	if (!(await allowed(ctx, c))) {
		c.result = 'the user refused the call';
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
// it stands: the model, its parameters, the system prompt, and the own tools
// of KiSS then those of every MCP server, the same from round to round while
// the configuration and the servers stay; a server down sits out the turn, and
// the pulse names the servers the round waits for; told gathers what the
// rounds of the turn left out, and the model reads all of it every round, so
// what it did about a server stays explained once the server is back
async function setup(
	messages: readonly Message[],
	signal: AbortSignal,
	p: Pulse,
	down: Map<string, string>,
	told: string[]
) {
	const { endpoint, model } = await pick(settings, signal);
	const waits = connecting(settings, down);
	if (waits.length) mark(p, 'preparing', `mcp ${waits.join(', ')}`, performance.now());
	const prompt = String(settings.get('chat system') ?? '');
	// the parameters set for this model, numbers as numbers
	const parameters = Object.fromEntries(
		Object.entries(models.keys).flatMap(([name, def]) => {
			const value = settings.get(`models ${name}`, endpoint.name + MODEL_SEPARATOR + model);
			return value === undefined ? [] : [[name, def.kind === 'number' ? Number(value) : value]];
		})
	);
	const { tools, problems } = await aggregate(settings, own, down);
	for (const problem of problems) if (!told.includes(problem)) told.push(problem);
	// the system prompt, then what the turn left out
	const system = [prompt, told.length ? [prompts.problems, ...told].join('\n') : '']
		.filter((part) => part)
		.join('\n\n');
	const request: Request = { model, system, messages, tools, parameters };
	return { endpoint, request, tools };
}

// one assistant turn: rounds streamed into reply as they arrive, until a round
// calls nothing; every round reads the configuration as it stands, so what a
// call changes holds from the next round on; reply belongs to the page state,
// so the round and its calls are read back from it once added; after a stop no
// call runs, and the reply keeps what settled before it; the pulse follows the
// turn chunk by chunk, and what it spent enters the reply once it streamed
export async function turn(
	messages: readonly Message[],
	reply: Assistant,
	ctx: ToolContext,
	signal: AbortSignal,
	p: Pulse
): Promise<void> {
	let r = 0;
	// the servers that failed this turn, as they were named, with what they
	// answered, and every tool the turn left out, as the model was told
	const down = new Map<string, string>();
	const told: string[] = [];
	try {
		for (; r < Number(settings.get('tools rounds')); r++) {
			p.round = r + 1;
			mark(p, 'preparing', '', performance.now());
			// the history as it stands before this round, which the round goes on
			// writing while the request streams
			const history = [...messages, { ...reply, rounds: [...reply.rounds] }];
			const { endpoint, request, tools } = await setup(history, signal, p, down, told);
			mark(p, 'waiting', endpoint.name, performance.now());
			reply.rounds.push({ reasoning: '', text: '', calls: [] });
			const round = reply.rounds[reply.rounds.length - 1];
			// the calls of the round by the index the stream gives them
			const calls = new Map<number, Call>();
			let usage: number | undefined;
			for await (const d of chat(endpoint, request, signal)) {
				const now = performance.now();
				if (d.usage !== undefined) usage = d.usage;
				chunk(p, !!(d.reasoning || d.content || d.calls), now);
				if (d.reasoning) {
					round.reasoning += d.reasoning;
					mark(p, 'thinking', '', now);
				}
				if (d.content) {
					round.text += d.content;
					mark(p, 'writing', '', now);
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
				await call(tools, ctx, c);
			}
		}
		reply.error = `stopped after ${r} tool rounds`;
	} finally {
		reply.rounds = settled(reply.rounds);
		const spent = stats(p, performance.now());
		if (spent) reply.stats = spent;
	}
}
