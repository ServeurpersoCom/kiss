import type { ConfigReader, Delta, Protocol, Request } from './types.js';
import { MODEL_SEPARATOR, SLASH } from './config.js';
import { seal } from './keychain.js';
import { remotes, type Remote } from './remote.js';
import chatProtocol from '../protocols/chat.js';

// the client of every endpoint: the model list, and a request streamed in the
// protocol the endpoint speaks

const UNAUTHORIZED = [401, 403];
// the statuses of an endpoint busy or down for a while: a timeout, too many
// requests, a server or gateway error, overloaded
const TRANSIENT = [408, 429, 500, 502, 503, 504, 529];
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

// an endpoint: a remote server, the protocol it speaks, the times a reply is
// asked again and the seconds between two of them
export interface Endpoint extends Remote {
	protocol: Protocol;
	retries: number;
	pause: number;
}

// the protocol an endpoint speaks
function protocol(config: ConfigReader, name: string): Protocol {
	return protocols.find((p) => p.name === config.get('endpoints protocol', name))!;
}

// the header that carries the key of an endpoint unless its auth says: the one
// of its protocol
export function fallback(config: ConfigReader, name: string): string {
	return protocol(config, name).key;
}

// every endpoint that has a url, sorted by name, each with its protocol
export function endpoints(config: ConfigReader): Endpoint[] {
	return remotes(config, 'endpoints', (name) => fallback(config, name)).map((r) => ({
		...r,
		protocol: protocol(config, r.name),
		retries: Number(config.get('endpoints retries', r.name)),
		pause: Number(config.get('endpoints pause', r.name))
	}));
}

// what the stream of a reply tells while it is asked again: the status that
// passes, the attempt about to be made, and how many the endpoint takes
export type Retrying = (status: number, attempt: number, attempts: number) => void;

// what a request carries: the key in the header its auth names, then the
// headers of the endpoint with those of its protocol; proxied, the key is the
// one of the KiSS server, which places the key of the endpoint itself
function sent(endpoint: Endpoint): Record<string, string> {
	return {
		...endpoint.sign(),
		...endpoint.protocol.headers(endpoint.headers)
	};
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

// the server's own JSON error message, else its text, as a KiSS server that
// proxies the endpoint tells what fails, else the HTTP status text: a web
// server that is not an LLM answers with a whole HTML page
async function failure(res: Response): Promise<EndpointError> {
	const body = await res.text();
	let message = res.statusText;
	try {
		message = JSON.parse(body).error?.message ?? message;
	} catch {
		if (res.headers.get('content-type')?.startsWith('text/plain')) message = body.trim() || message;
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
			headers: { ...sent(endpoint), ...init.headers },
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

// what an endpoint lists of every model it serves, each with its id
type Listed = { id: string } & Record<string, unknown>;

function list(endpoint: Endpoint, signal?: AbortSignal): Promise<Listed[]> {
	return request(endpoint, endpoint.protocol.models, {}, signal, async (res) => {
		if (!res.ok) throw await failure(res);
		if (!res.headers.get('content-type')?.includes('json')) throw notAnLlm(res);
		const json = await res.json();
		return json.data ?? [];
	});
}

// the models an endpoint serves
export async function listModels(endpoint: Endpoint, signal?: AbortSignal): Promise<string[]> {
	return (await list(endpoint, signal)).map((m) => m.id);
}

// the list of every endpoint as it last answered, by where it goes and with
// what key, for the life of the page; a list that failed is asked again
const lists = new Map<string, Promise<Listed[]>>();

// what an endpoint lists of one model, none when it lists no such model
export async function describe(
	endpoint: Endpoint,
	model: string,
	signal?: AbortSignal
): Promise<Listed | undefined> {
	const id = `${endpoint.url}${endpoint.protocol.models} ${JSON.stringify(endpoint.sign())}`;
	let all = lists.get(id);
	if (!all) {
		all = list(endpoint, signal);
		lists.set(id, all);
		all.catch(() => lists.delete(id));
	}
	return (await all).find((m) => m.id === model);
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

// the time given, or less when the signal aborts
function sleep(ms: number, signal: AbortSignal): Promise<void> {
	return new Promise((resolve, reject) => {
		const timer = setTimeout(resolve, ms);
		signal.addEventListener('abort', () => (clearTimeout(timer), reject(signal.reason)), {
			once: true
		});
	});
}

// the body of a reply, asked again while the endpoint answers a status that
// passes, as many times as it takes; nothing streamed yet, nothing repeats
async function opened(
	endpoint: Endpoint,
	init: RequestInit,
	signal: AbortSignal,
	retrying: Retrying
) {
	for (let attempt = 1; ; attempt++) {
		const answer = await request(endpoint, endpoint.protocol.path, init, signal, async (res) => {
			if (attempt <= endpoint.retries && TRANSIENT.includes(res.status)) return res;
			if (!res.ok || !res.body) throw await failure(res);
			if (res.headers.get('content-type')?.includes('html')) throw notAnLlm(res);
			return res.body;
		});
		if (!(answer instanceof Response)) return answer;
		await answer.body?.cancel();
		retrying(answer.status, attempt, endpoint.retries);
		await sleep(endpoint.pause * 1000, signal);
	}
}

// one delta per server sent event, read by the protocol of the endpoint from
// the data of the event, its data lines joined by LF, once the blank line that
// closes it came, up to the event that ends the stream, which may carry the
// last count: a stream closed before it is cut short, never a finished answer,
// and an event it leaves open is never read
export async function* chat(
	endpoint: Endpoint,
	req: Request,
	signal: AbortSignal,
	retrying: Retrying
): AsyncGenerator<Delta> {
	const { protocol } = endpoint;
	const init = {
		method: 'POST',
		headers: { 'Content-Type': 'application/json' },
		// what the model reads leaves sealed, whatever the history holds
		body: seal(protocol.body(req))
	};
	const stream = await opened(endpoint, init, signal, retrying);
	const reader = stream.pipeThrough(new TextDecoderStream()).getReader();
	const read = protocol.reader();
	let buffer = '';
	let data: string[] = [];
	for (;;) {
		const { value, done } = await reader.read();
		if (done) throw new Error(`the stream ended before ${protocol.last}`);
		buffer += value;
		let nl: number;
		while ((nl = buffer.indexOf('\n')) >= 0) {
			const line = buffer.slice(0, nl).replace(/\r$/, '');
			buffer = buffer.slice(nl + 1);
			if (line.startsWith(DATA)) data.push(line.slice(DATA.length));
			if (line || !data.length) continue;
			const event = data.join('\n').trim();
			data = [];
			let d: Delta | undefined;
			try {
				d = read(event);
			} catch {
				throw new Error(`${endpoint.url} sent an event that does not read: ${event}`);
			}
			if (!d) continue;
			if (d.error !== undefined) throw new Error(d.error || `${endpoint.url} broke the stream`);
			yield d;
			if (d.end) return;
		}
	}
}
