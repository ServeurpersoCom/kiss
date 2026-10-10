import type { ConfigReader, Delta, Protocol, Request } from './types.js';
import { MODEL_SEPARATOR, SLASH } from './config.js';
import { remotes, type Remote } from './remote.js';
import chatProtocol from '../protocols/chat.js';

// the client of every endpoint: the model list, and a request streamed in the
// protocol the endpoint speaks

const UNAUTHORIZED = [401, 403];
const NOT_FOUND = 404;
// the commands that fix an endpoint which refuses the key, or is no LLM
const KEY_FIX = `${SLASH}set endpoints key <name> <key>`;
const URL_FIX = `${SLASH}set endpoints url <name> <url>`;
const DATA = 'data:';

// the protocols of KiSS: a file in protocols/ offers its protocol by existing
const files = import.meta.glob<{ default: Protocol }>('../protocols/*.ts', { eager: true });
const protocols = Object.values(files).map((f) => f.default);

// the names endpoints protocol takes, and the one an endpoint speaks unless set
export const PROTOCOLS: readonly string[] = protocols.map((p) => p.name);
export const DEFAULT_PROTOCOL = chatProtocol.name;

// an endpoint: a remote server and the protocol it speaks
export interface Endpoint extends Remote {
	protocol: Protocol;
}

// every endpoint that has a url, sorted by name, each with its protocol
export function endpoints(config: ConfigReader): Endpoint[] {
	return remotes(config, 'endpoints').map((r) => ({
		...r,
		protocol: protocols.find((p) => p.name === config.get('endpoints protocol', r.name))!
	}));
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
// body streams on that signal alone; it carries the key as the protocol does,
// then the headers of the endpoint, which may override it
async function request<T>(
	endpoint: Endpoint,
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
			headers: { ...endpoint.protocol.auth(endpoint.key), ...endpoint.headers, ...init.headers },
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
export function listModels(endpoint: Endpoint, signal?: AbortSignal): Promise<string[]> {
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
): Promise<{ endpoint: Endpoint; model: string }> {
	const all = endpoints(config);
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

// one delta per server sent event, read by the protocol of the endpoint from
// the data of the event, until the event that ends the stream: a stream closed
// before it is cut short, never a finished answer
export async function* chat(
	endpoint: Endpoint,
	req: Request,
	signal: AbortSignal
): AsyncGenerator<Delta> {
	const { protocol } = endpoint;
	const init = {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		body: JSON.stringify(protocol.body(req))
	};
	const stream = await request(endpoint, protocol.path, init, signal, async (res) => {
		if (!res.ok || !res.body) throw await failure(res);
		if (res.headers.get('content-type')?.includes('html')) throw notAnLlm(res);
		return res.body;
	});
	const reader = stream.pipeThrough(new TextDecoderStream()).getReader();
	const read = protocol.reader();
	let buffer = '';
	for (;;) {
		const { value, done } = await reader.read();
		if (done) throw new Error(`the stream ended before ${protocol.last}`);
		buffer += value;
		let nl: number;
		while ((nl = buffer.indexOf('\n')) >= 0) {
			const line = buffer.slice(0, nl).trim();
			buffer = buffer.slice(nl + 1);
			if (!line.startsWith(DATA)) continue;
			const d = read(line.slice(DATA.length).trim());
			if (!d) continue;
			if (d.error !== undefined) throw new Error(d.error || `${endpoint.url} broke the stream`);
			if (d.end) return;
			yield d;
		}
	}
}
