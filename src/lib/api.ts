import type { ConfigReader } from './types.js';
import { MODEL_SEPARATOR, SLASH } from './config.js';
import { bearer, remotes, type Remote } from './remote.js';

// OpenAI compatible client: the model list and streamed chat completions

const UNAUTHORIZED = [401, 403];
const NOT_FOUND = 404;
// the commands that fix an endpoint which refuses the key, or is no LLM
const KEY_FIX = `${SLASH}set endpoints key <name> <key>`;
const URL_FIX = `${SLASH}set endpoints url <name> <url>`;

export interface Delta {
	content?: string;
	reasoning?: string;
	calls?: { index: number; id?: string; name?: string; args?: string }[];
}

// what an endpoint answered wrong, with the command that fixes it when the
// configuration is the cause
export class EndpointError extends Error {
	constructor(
		message: string,
		readonly fix?: string
	) {
		super(message);
	}
}

// the server's own JSON error message, else the HTTP status text: a web server
// that is not an LLM answers with a whole HTML page
async function failure(res: Response): Promise<EndpointError> {
	const body = await res.text();
	let message = res.statusText;
	try {
		message = JSON.parse(body).error?.message ?? message;
	} catch {
		// not JSON
	}
	const fix = UNAUTHORIZED.includes(res.status)
		? KEY_FIX
		: res.status === NOT_FOUND
			? URL_FIX
			: undefined;
	return new EndpointError(`${res.url} answers ${res.status} ${message}`, fix);
}

// a static web server answers every path with a page: no LLM behind that URL
function notAnLlm(res: Response): EndpointError {
	return new EndpointError(`${res.url} answers a web page, not an LLM`, URL_FIX);
}

// a request the endpoint answers within its timeout, as far as read takes the
// answer, or less when the signal of the caller aborts; what read leaves of a
// body streams on that signal alone
async function request<T>(
	endpoint: Remote,
	path: string,
	init: RequestInit,
	signal: AbortSignal | undefined,
	read: (res: Response) => Promise<T>
): Promise<T> {
	const start = new AbortController();
	const timer = setTimeout(
		() => start.abort(new Error(`${endpoint.url} does not answer within ${endpoint.timeout} s`)),
		endpoint.timeout * 1000
	);
	try {
		const res = await fetch(`${endpoint.url}${path}`, {
			...init,
			headers: { ...bearer(endpoint.key), ...init.headers },
			signal: signal ? AbortSignal.any([signal, start.signal]) : start.signal
		}).catch((e: Error) => {
			// a network or CORS failure: nothing answers at that URL
			throw e instanceof TypeError
				? new EndpointError(`${endpoint.url} does not answer`, URL_FIX)
				: e;
		});
		return await read(res);
	} finally {
		clearTimeout(timer);
	}
}

// the models an endpoint serves
export function listModels(endpoint: Remote, signal?: AbortSignal): Promise<string[]> {
	return request(endpoint, '/models', {}, signal, async (res) => {
		if (!res.ok) throw await failure(res);
		if (!res.headers.get('content-type')?.includes('json')) throw notAnLlm(res);
		const json = await res.json();
		return (json.data ?? []).map((m: { id: string }) => m.id);
	});
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
		throw new EndpointError('no endpoint yet', URL_FIX);
	}
	const value = String(config.get('chat model') ?? '');
	if (!value) {
		const models = all.length === 1 ? await listModels(all[0], signal) : [];
		if (models.length !== 1) {
			throw new Error(
				`chat model is not set -> ${SLASH}show models, then ${SLASH}set chat model <endpoint/model>`
			);
		}
		return { endpoint: all[0], model: models[0] };
	}
	const cut = value.indexOf(MODEL_SEPARATOR);
	const endpoint = all.find((e) => e.name === value.slice(0, cut));
	if (cut < 0 || !endpoint) {
		throw new Error(
			`chat model ${value} names no endpoint -> ${SLASH}show models lists every endpoint/model`
		);
	}
	return { endpoint, model: value.slice(cut + MODEL_SEPARATOR.length) };
}

// one delta per server sent event, until [DONE]: a stream closed before it is
// cut short, never a finished answer
export async function* chat(
	endpoint: Remote,
	body: object,
	signal: AbortSignal
): AsyncGenerator<Delta> {
	const init = {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		body: JSON.stringify({ ...body, stream: true })
	};
	const stream = await request(endpoint, '/chat/completions', init, signal, async (res) => {
		if (!res.ok || !res.body) throw await failure(res);
		if (res.headers.get('content-type')?.includes('html')) throw notAnLlm(res);
		return res.body;
	});
	const reader = stream.pipeThrough(new TextDecoderStream()).getReader();
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
			if (json.error) throw new Error(json.error.message ?? `${endpoint.url} broke the stream`);
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
