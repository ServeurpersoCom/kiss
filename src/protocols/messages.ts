import type { Message, Protocol } from '../lib/types.js';
import { SLASH } from '../lib/config.js';

// the Messages API of Anthropic: content blocks streamed by index, thinking
// summarized and signed, sent back as it came; the prompt cached up to its
// last block, the cache moving with the conversation

const NAME = 'messages';
const VERSION = '2023-06-01';
// the parameters of models sent as they are named
const CARRIED = ['temperature', 'top_p', 'top_k'] as const;
const THINKING = { type: 'adaptive', display: 'summarized' };
const CACHE = { type: 'ephemeral' };

type Block = Record<string, unknown>;
interface Turn {
	role: 'user' | 'assistant';
	content: Block[];
}

// the arguments of a call as an object, none when they read as anything else
function input(args: string): object {
	try {
		const value: unknown = JSON.parse(args);
		return value && typeof value === 'object' && !Array.isArray(value) ? value : {};
	} catch {
		return {};
	}
}

// the history in blocks: cli messages stay out; every round of an assistant
// turn becomes the blocks its protocol kept when it is this one, its text and
// its calls, then a user turn of their results; turns of one role in a row
// merge, as the API takes one role after the other
function history(messages: readonly Message[]): Turn[] {
	const turns: Turn[] = [];
	const add = (role: Turn['role'], content: Block[]) => {
		const last = turns[turns.length - 1];
		if (!content.length) return;
		if (last?.role === role) last.content.push(...content);
		else turns.push({ role, content });
	};
	for (const m of messages) {
		if (m.role === 'user') add('user', [{ type: 'text', text: m.text }]);
		if (m.role !== 'assistant') continue;
		for (const r of m.rounds) {
			add('assistant', [
				...((r.opaque?.protocol === NAME ? r.opaque.items : []) as Block[]),
				...(r.text ? [{ type: 'text', text: r.text }] : []),
				...r.calls.map((c) => ({ type: 'tool_use', id: c.id, name: c.name, input: input(c.args) }))
			]);
			add(
				'user',
				r.calls.map((c) => ({
					type: 'tool_result',
					tool_use_id: c.id,
					content: c.result ?? '',
					...(c.ok === false ? { is_error: true } : {})
				}))
			);
		}
	}
	return turns;
}

export default {
	name: NAME,
	path: '/messages',
	auth: (key) => ({
		...(key ? { 'x-api-key': key } : {}),
		'anthropic-version': VERSION,
		'anthropic-dangerous-direct-browser-access': 'true'
	}),
	drops: ['min_p', 'presence_penalty', 'frequency_penalty', 'seed'],
	body(r) {
		const p = r.parameters;
		if (p.max_tokens === undefined) {
			throw new Error(
				`${NAME} needs models max_tokens -> ${SLASH}set models max_tokens <endpoint/model> <tokens>`
			);
		}
		return {
			model: r.model,
			max_tokens: p.max_tokens,
			...Object.fromEntries(CARRIED.filter((k) => p[k] !== undefined).map((k) => [k, p[k]])),
			thinking: THINKING,
			...(p.reasoning_effort !== undefined
				? { output_config: { effort: p.reasoning_effort } }
				: {}),
			cache_control: CACHE,
			...(r.system ? { system: r.system } : {}),
			messages: history(r.messages),
			...(r.tools.length
				? {
						tools: r.tools.map((t) => ({
							name: t.name,
							description: t.description,
							input_schema: t.parameters,
							eager_input_streaming: true
						}))
					}
				: {}),
			stream: true
		};
	},
	last: 'message_stop',
	// a thinking block is kept once it closes, its text and its signature whole;
	// a redacted one as it opens; the tokens counted so far come with every
	// change of the message
	reader() {
		const thinking = new Map<number, { type: string; thinking: string; signature: string }>();
		return (data) => {
			const e = JSON.parse(data);
			if (e.type === 'content_block_start') {
				const b = e.content_block;
				if (b.type === 'thinking') {
					thinking.set(e.index, { type: b.type, thinking: '', signature: '' });
				}
				if (b.type === 'redacted_thinking') return { opaque: b };
				if (b.type === 'tool_use') return { calls: [{ index: e.index, id: b.id, name: b.name }] };
				if (b.type === 'text' && b.text) return { content: b.text };
				return undefined;
			}
			if (e.type === 'content_block_delta') {
				const d = e.delta;
				if (d.type === 'text_delta') return { content: d.text };
				if (d.type === 'input_json_delta') {
					return { calls: [{ index: e.index, args: d.partial_json }] };
				}
				const block = thinking.get(e.index);
				if (block && d.type === 'thinking_delta') {
					block.thinking += d.thinking;
					return { reasoning: d.thinking };
				}
				if (block && d.type === 'signature_delta') block.signature += d.signature;
				return undefined;
			}
			if (e.type === 'content_block_stop') {
				const block = thinking.get(e.index);
				return block ? { opaque: block } : undefined;
			}
			if (e.type === 'message_delta') {
				const usage = e.usage?.output_tokens;
				return typeof usage === 'number' ? { usage } : undefined;
			}
			if (e.type === 'message_stop') return { end: true };
			if (e.type === 'error') return { error: e.error?.message ?? '' };
			return undefined;
		};
	}
} satisfies Protocol;
