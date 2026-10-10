import type { Message, Protocol } from '../lib/types.js';
import { bearer } from '../lib/remote.js';

// the Responses API of OpenAI, stateless: the history goes whole as items
// every round, nothing stored on the server; the reasoning streams as its
// summary and goes back as it came, encrypted

const NAME = 'responses';
// the parameters of models sent as they are named
const CARRIED = ['temperature', 'top_p'] as const;
const SUMMARY = 'auto';
// the parts of a summary as they read one after the other
const PARTS = '\n\n';

type Item = Record<string, unknown>;

// the history in items: cli messages stay out; every round of an assistant
// turn becomes the reasoning its protocol kept when it is this one, its text
// as one message in the phase it came in, its calls, then their outputs
function history(messages: readonly Message[]): Item[] {
	return messages.flatMap((m): Item[] => {
		if (m.role === 'user') return [{ role: 'user', content: m.text }];
		if (m.role === 'cli') return [];
		return m.rounds.flatMap((r) => {
			const kept = (r.opaque?.protocol === NAME ? r.opaque.items : []) as Item[];
			const phase = kept.filter((i) => i.type === 'message').at(-1)?.phase;
			return [
				...kept.filter((i) => i.type === 'reasoning'),
				...(r.text ? [{ role: 'assistant', content: r.text, ...(phase ? { phase } : {}) }] : []),
				...r.calls.map((c) => ({
					type: 'function_call',
					call_id: c.id,
					name: c.name,
					arguments: c.args
				})),
				...r.calls.map((c) => ({
					type: 'function_call_output',
					call_id: c.id,
					output: c.result ?? ''
				}))
			];
		});
	});
}

export default {
	name: NAME,
	path: '/responses',
	models: '/models',
	described: false,
	auth: bearer,
	drops: ['top_k', 'min_p', 'presence_penalty', 'frequency_penalty', 'seed'],
	body(r) {
		const p = r.parameters;
		return {
			model: r.model,
			...(r.system ? { instructions: r.system } : {}),
			input: history(r.messages),
			...(r.tools.length
				? {
						tools: r.tools.map((t) => ({
							type: 'function',
							name: t.name,
							description: t.description,
							parameters: t.parameters,
							strict: false
						}))
					}
				: {}),
			...Object.fromEntries(CARRIED.filter((k) => p[k] !== undefined).map((k) => [k, p[k]])),
			...(p.max_tokens !== undefined ? { max_output_tokens: p.max_tokens } : {}),
			reasoning: {
				...(p.reasoning_effort !== undefined ? { effort: p.reasoning_effort } : {}),
				summary: SUMMARY
			},
			store: false,
			stream: true
		};
	},
	last: 'response.completed',
	// a call by the index of its item, its arguments as they stream; a
	// reasoning item kept once done, whole, and the phase of a message; the
	// tokens counted come with the end
	reader: () => (data) => {
		const e = JSON.parse(data);
		const item = e.item;
		switch (e.type) {
			case 'response.output_item.added':
				return item.type === 'function_call'
					? { calls: [{ index: e.output_index, id: item.call_id, name: item.name }] }
					: undefined;
			case 'response.function_call_arguments.delta':
				return { calls: [{ index: e.output_index, args: e.delta }] };
			case 'response.output_text.delta':
				return { content: e.delta };
			case 'response.reasoning_summary_text.delta':
			case 'response.reasoning_text.delta':
				return { reasoning: e.delta };
			case 'response.reasoning_summary_part.added':
				return e.summary_index > 0 ? { reasoning: PARTS } : undefined;
			case 'response.output_item.done':
				if (item.type === 'reasoning') return { opaque: item };
				if (item.type === 'message' && item.phase) {
					return { opaque: { type: 'message', phase: item.phase } };
				}
				return undefined;
			case 'response.completed':
			case 'response.incomplete': {
				const usage = e.response?.usage?.output_tokens;
				return { ...(typeof usage === 'number' ? { usage } : {}), end: true };
			}
			case 'response.failed':
				return { error: e.response?.error?.message ?? '' };
			case 'error':
				return { error: e.message ?? '' };
		}
		return undefined;
	}
} satisfies Protocol;
