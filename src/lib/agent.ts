import type { Assistant, Call, Message, Outcome, Tool, ToolContext } from './types.js';
import { ALWAYS, REFUSE } from './types.js';
import { chat, pick } from './api.js';
import { aggregate, connecting } from './mcp.js';
import prompts from './prompts.json';
import { settled } from './conversation.js';
import { MODEL_SEPARATOR } from './config.js';
import { chunk, close, mark, stats, type Pulse } from './pulse.js';
import { CONSENT, ON, tools as own } from './tools.js';
import models from '../modules/models.js';
import { run, settings } from '../engine/run.js';

// the chat history in OpenAI form: cli messages stay out, and every round of
// an assistant turn becomes one assistant message followed by its results
function history(messages: readonly Message[]): object[] {
	return messages.flatMap((m) => {
		if (m.role === 'user') return [{ role: 'user', content: m.text }];
		if (m.role === 'cli') return [];
		return m.rounds.flatMap((r) => [
			{
				role: 'assistant',
				content: r.text,
				...(r.reasoning ? { reasoning_content: r.reasoning } : {}),
				...(r.calls.length
					? {
							tool_calls: r.calls.map((c) => ({
								id: c.id,
								type: 'function',
								function: { name: c.name, arguments: c.args }
							}))
						}
					: {})
			},
			...r.calls.map((c) => ({ role: 'tool', tool_call_id: c.id, content: c.result ?? '' }))
		]);
	});
}

// whether the user lets the model make this call: a tool in consent asks, and
// always turns it on, as the user
async function allowed(ctx: ToolContext, c: Call): Promise<boolean> {
	if (settings.get('tools use', c.name) !== CONSENT) return true;
	const verdict = await ctx.grant({ kind: 'call', tool: c.name, args: c.args });
	ctx.signal.throwIfAborted();
	if (verdict === ALWAYS) await run(`set tools use ${JSON.stringify(c.name)} ${ON}`, 'user');
	return verdict !== REFUSE;
}

// runs one call and writes its outcome into it; a call takes its arguments as
// its tool keeps them, and is stored as sent before it reaches its tool, so a
// stop or a page closed while it runs leaves it stopped, whatever the tool
// answers
async function call(tools: readonly Tool[], ctx: ToolContext, c: Call): Promise<void> {
	const tool = tools.find((t) => t.name === c.name);
	if (!tool) {
		c.result = `unknown tool "${c.name}"`;
		c.ok = false;
		return;
	}
	let args: Record<string, unknown>;
	try {
		args = c.args ? JSON.parse(c.args) : {};
	} catch {
		c.result = 'the arguments are not JSON';
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
	// one system message, first, as every template takes it: the system prompt,
	// then what the turn left out
	const system = [prompt, told.length ? [prompts.problems, ...told].join('\n') : '']
		.filter((part) => part)
		.join('\n\n');
	const body = {
		...parameters,
		model,
		messages: [...(system ? [{ role: 'system', content: system }] : []), ...history(messages)],
		tools: tools.map((t) => ({
			type: 'function',
			function: { name: t.name, description: t.description, parameters: t.parameters }
		}))
	};
	return { endpoint, body, tools };
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
			const { endpoint, body, tools } = await setup([...messages, reply], signal, p, down, told);
			mark(p, 'waiting', endpoint.name, performance.now());
			reply.rounds.push({ reasoning: '', text: '', calls: [] });
			const round = reply.rounds[reply.rounds.length - 1];
			// the calls of the round by the index the stream gives them
			const calls = new Map<number, Call>();
			let usage: number | undefined;
			for await (const d of chat(endpoint, body, signal)) {
				const now = performance.now();
				if (d.usage !== undefined) {
					usage = d.usage;
					continue;
				}
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
