import type { Message, Protocol } from '../lib/types.js';
import { bearer } from '../lib/remote.js';

// OpenAI chat completions, the open standard of llama.cpp, vLLM and Hugging
// Face: one delta per event until [DONE], the endpoint counting the tokens it
// generated on a last chunk of its own

const DONE = '[DONE]';

// the history in OpenAI form: cli messages stay out, and every round of an
// assistant turn becomes one assistant message followed by its results
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

export default {
	name: 'chat',
	path: '/chat/completions',
	auth: bearer,
	drops: [],
	// one system message, first, as every template takes it
	body: (r) => ({
		...r.parameters,
		model: r.model,
		messages: [
			...(r.system ? [{ role: 'system', content: r.system }] : []),
			...history(r.messages)
		],
		tools: r.tools.map((t) => ({
			type: 'function',
			function: { name: t.name, description: t.description, parameters: t.parameters }
		})),
		stream: true,
		stream_options: { include_usage: true }
	}),
	last: DONE,
	reader: () => (data) => {
		if (data === DONE) return { end: true };
		const json = JSON.parse(data);
		if (json.error) return { error: json.error.message ?? '' };
		const usage = json.usage?.completion_tokens;
		const count = typeof usage === 'number' ? { usage } : {};
		const delta = json.choices?.[0]?.delta;
		if (!delta) return typeof usage === 'number' ? count : undefined;
		return {
			...count,
			content: delta.content ?? undefined,
			reasoning: delta.reasoning_content ?? undefined,
			calls: delta.tool_calls?.map(
				(c: { index: number; id?: string; function?: { name?: string; arguments?: string } }) => ({
					index: c.index,
					id: c.id,
					name: c.function?.name,
					args: c.function?.arguments
				})
			)
		};
	}
} satisfies Protocol;
