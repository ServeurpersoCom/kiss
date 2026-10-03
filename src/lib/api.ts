import type { ConfigReader } from './types.js';
import { MODELS_TIMEOUT_MS } from './config.js';
import { bearer, remotes, type Remote } from './remote.js';

// OpenAI compatible client: the model list and streamed chat completions

export interface Delta {
	content?: string;
	reasoning?: string;
	calls?: { index: number; id?: string; name?: string; args?: string }[];
}

// the server's own JSON error message, else the HTTP status text: a web server
// that is not an LLM answers with a whole HTML page
async function failure(res: Response): Promise<Error> {
	const body = await res.text();
	let message = res.statusText;
	try {
		message = JSON.parse(body).error?.message ?? message;
	} catch {
		// not JSON
	}
	return Object.assign(new Error(`${res.url} answers ${res.status} ${message}`), {
		status: res.status
	});
}

// a static web server answers every path with a page: no LLM behind that URL
function notAnLlm(res: Response): Error {
	return Object.assign(new Error(`${res.url} answers a web page, not an LLM`), { status: 404 });
}

// the models an endpoint serves, within MODELS_TIMEOUT_MS or sooner when the
// signal of the caller aborts
export async function listModels(
	url: string,
	key: string,
	signal?: AbortSignal
): Promise<string[]> {
	const timeout = AbortSignal.timeout(MODELS_TIMEOUT_MS);
	const res = await fetch(`${url}/models`, {
		headers: bearer(key),
		signal: signal ? AbortSignal.any([signal, timeout]) : timeout
	});
	if (!res.ok) throw await failure(res);
	if (!res.headers.get('content-type')?.includes('json')) throw notAnLlm(res);
	const json = await res.json();
	return (json.data ?? []).map((m: { id: string }) => m.id);
}

// the endpoint and model id chat model asks for, written endpoint/model; empty,
// the one model of the one endpoint, any other choice being the user's: a
// router loads whatever model it is asked for
export async function pick(
	config: ConfigReader,
	signal?: AbortSignal
): Promise<{ endpoint: Remote; model: string }> {
	const all = remotes(config, 'endpoints');
	if (!all.length) {
		throw Object.assign(new Error('no endpoint yet'), { status: 404 });
	}
	const value = String(config.get('chat model') ?? '');
	if (!value) {
		const models = all.length === 1 ? await listModels(all[0].url, all[0].key, signal) : [];
		if (models.length !== 1) {
			throw new Error('chat model is not set: /set chat model <endpoint/model>, see /show models');
		}
		return { endpoint: all[0], model: models[0] };
	}
	const slash = value.indexOf('/');
	const endpoint = all.find((e) => e.name === value.slice(0, slash));
	if (slash < 0 || !endpoint) {
		throw new Error(
			`chat model ${value} names no endpoint: write it endpoint/model, as show models lists it`
		);
	}
	return { endpoint, model: value.slice(slash + 1) };
}

// one delta per server sent event, until [DONE]: a stream closed before it is
// cut short, never a finished answer
export async function* chat(
	url: string,
	key: string,
	body: object,
	signal: AbortSignal
): AsyncGenerator<Delta> {
	const res = await fetch(`${url}/chat/completions`, {
		method: 'POST',
		headers: { ...bearer(key), 'Content-Type': 'application/json' },
		body: JSON.stringify({ ...body, stream: true }),
		signal
	});
	if (!res.ok || !res.body) throw await failure(res);
	if (res.headers.get('content-type')?.includes('html')) throw notAnLlm(res);
	const reader = res.body.pipeThrough(new TextDecoderStream()).getReader();
	let buffer = '';
	for (;;) {
		const { value, done } = await reader.read();
		if (done) throw new Error('the stream ended before [DONE]');
		buffer += value;
		let nl: number;
		while ((nl = buffer.indexOf('\n')) >= 0) {
			const line = buffer.slice(0, nl).trim();
			buffer = buffer.slice(nl + 1);
			if (!line.startsWith('data:')) continue;
			const data = line.slice(5).trim();
			if (data === '[DONE]') return;
			const json = JSON.parse(data);
			if (json.error) throw new Error(json.error.message ?? 'stream error');
			const delta = json.choices?.[0]?.delta;
			if (!delta) continue;
			yield {
				content: delta.content ?? undefined,
				reasoning: delta.reasoning_content ?? undefined,
				calls: delta.tool_calls?.map(
					(c: {
						index: number;
						id?: string;
						function?: { name?: string; arguments?: string };
					}) => ({
						index: c.index,
						id: c.id,
						name: c.function?.name,
						args: c.function?.arguments
					})
				)
			};
		}
	}
}
