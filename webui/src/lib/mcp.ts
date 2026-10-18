import {
	Client,
	SdkHttpError,
	StreamableHTTPClientTransport,
	isInputRequiredResult
} from '@modelcontextprotocol/client';
import type {
	CallToolRequest,
	CallToolResult,
	InputRequiredResult
} from '@modelcontextprotocol/client';
import type { Image, Outcome, Tool, ToolContext } from './types.js';
import type { ConfigReader } from '../engine/engine.js';
import { MCP_CONNECT_TIMEOUT_MS, NAME, SYSTEM, capital } from './config.js';
import { headers, remotes, type Remote } from './remote.js';
import { OFF } from './tools.js';
import { fill } from './form.js';

// the MCP servers of the configuration, over Streamable HTTP: one client per
// server, kept while its url and key stay, with the tool list it serves, asked
// again as each turn starts, so a tool a server gains shows from the next turn
// on and the tools the model sees stay the same from round to round; a
// request that fails drops the client, and the next use connects again; the
// tool list a server gave last stays while it is named the same, so a server
// that fails goes on serving it, each call answering its error

// a client, the signature of the server it connected to, its tool list, and
// whether that list came
interface Connection {
	remote: Remote;
	signed: string;
	tools: Promise<Tool[]>;
	client: Promise<Client>;
	settled: boolean;
}

const connections = new Map<string, Connection>();

// the tools each server listed last, with the signature it was named under
const kept = new Map<string, { signed: string; tools: Tool[] }>();

// the HTTP status of a server that takes no request without the right key
const UNAUTHORIZED = 401;

// a route a KiSS server publishes as a resource: kiss:proxy/<name>, its _meta
// telling whether the route holds a key
const ROUTE_URI = 'kiss:proxy/';
const ROUTE_KEYED_META = 'kiss/keyed';

// what the server answered, or did not, with the tools it listed last; a KiSS
// server also gives the line it writes of itself, server forge 5633ee7
// (2026-10-11) on linux; locked, it answered that it takes no request without
// the right key
interface Served {
	server: string;
	tools?: Tool[];
	system?: string;
	error?: string;
	locked?: boolean;
}

// the line a KiSS server gives of itself, none from any other server
function system(client: Client): { system?: string } {
	const info = client.getServerVersion();
	return info?.title === SYSTEM && info.description ? { system: info.description } : {};
}

function drop(name: string): void {
	const c = connections.get(name);
	connections.delete(name);
	void c?.client.then((client) => client.close()).catch(() => undefined);
}

// a 2026 server speaks its own era, a 2025 one answers the initialize handshake;
// a server may ask the user for secrets, which a call answers itself
function connect(remote: Remote): Promise<Client> {
	const client = new Client(
		{ name: 'kiss', title: NAME, version: __KISS_VERSION__ },
		{
			versionNegotiation: { mode: 'auto' },
			capabilities: { elicitation: { form: {} } },
			inputRequired: { autoFulfill: false }
		}
	);
	const transport = new StreamableHTTPClientTransport(new URL(remote.url, location.href), {
		requestInit: { headers: headers(remote) }
	});
	return client.connect(transport, { timeout: MCP_CONNECT_TIMEOUT_MS }).then(() => client);
}

// the size of the data base64 encodes
function bytes(base64: string): number {
	const pad = base64.endsWith('==') ? 2 : base64.endsWith('=') ? 1 : 0;
	return (base64.length / 4) * 3 - pad;
}

// the content blocks as the model reads them: text as is, every other block a
// line naming it; images also go to the user, and to the model that sees them,
// their line telling the one that does not
function outcome(result: CallToolResult): Outcome {
	const lines: string[] = [];
	const images: Image[] = [];
	for (const block of result.content) {
		if (block.type === 'text') lines.push(block.text);
		else if (block.type === 'image') {
			images.push({ mime: block.mimeType, data: block.data });
			lines.push(`Image ${block.mimeType}, ${bytes(block.data)} bytes`);
		} else lines.push(capital(`${block.type} content`));
	}
	return { ok: !result.isError, text: lines.join('\n'), ...(images.length ? { images } : {}) };
}

// a call to a tool of a server, asked again with the answers of the user each
// time the server asks them for secrets; every round asks the user, so one
// that asks nothing is an error, and a stopped turn asks nothing more
async function called(
	ctx: ToolContext,
	remote: Remote,
	client: Client,
	params: CallToolRequest['params']
): Promise<Outcome> {
	const options = { signal: ctx.signal, timeout: remote.timeout * 1000, allowInputRequired: true };
	let result: CallToolResult | InputRequiredResult = await client.callTool(params, options);
	while (isInputRequiredResult(result)) {
		const inputs = Object.entries(result.inputRequests ?? {});
		if (!inputs.length) throw new Error('asks for input and names none');
		const inputResponses: Record<string, object> = {};
		for (const [key, input] of inputs) {
			ctx.signal.throwIfAborted();
			inputResponses[key] = await fill(ctx, remote.name, input);
		}
		const again = { ...params, inputResponses, requestState: result.requestState };
		result = await client.callTool(again as CallToolRequest['params'], options);
	}
	return outcome(result);
}

// a server as the configuration names it: its name, url, key, headers and
// timeout; a change of any makes it another server
const signature = (remote: Remote): string => JSON.stringify([remote, remote.sign()]);

// the tools a client lists now, each called through that client
async function listed(remote: Remote, c: Client): Promise<Tool[]> {
	const { tools } = await c.listTools(undefined, { timeout: MCP_CONNECT_TIMEOUT_MS });
	return tools.map((t): Tool => ({
		name: t.name,
		description: t.description ?? '',
		input: t.inputSchema,
		async run(ctx, args) {
			try {
				return await called(ctx, remote, c, { name: t.name, arguments: args });
			} catch (e) {
				if (ctx.signal.aborted) throw e;
				drop(remote.name);
				throw new Error(`mcp ${remote.name}: ${(e as Error).message}`);
			}
		}
	}));
}

// a list that fails drops the client it came from, unless another one took
// its place
function watch(made: Connection): void {
	made.tools.then(
		() => (made.settled = true),
		() => connections.get(made.remote.name) === made && drop(made.remote.name)
	);
}

function connection(remote: Remote): Connection {
	const held = connections.get(remote.name);
	const signed = signature(remote);
	if (held && held.signed === signed) return held;
	if (held) drop(remote.name);
	const client = connect(remote);
	const made: Connection = {
		remote,
		signed,
		client,
		tools: client.then((c) => listed(remote, c)),
		settled: false
	};
	connections.set(remote.name, made);
	watch(made);
	return made;
}

// a turn starts on the tools every connected server serves now, each listing
// them again on the client it holds
export function relist(): void {
	for (const made of connections.values()) {
		if (!made.settled) continue;
		made.tools = made.client.then((c) => listed(made.remote, c));
		watch(made);
	}
}

// the servers of the configuration a use waits for: those not down that hold
// no tool list yet, connecting or about to
export function connecting(config: ConfigReader, down: ReadonlyMap<string, string>): string[] {
	return remotes(config, 'mcp')
		.filter((r) => !down.has(signature(r)) && !connections.get(r.name)?.settled)
		.map((r) => r.name);
}

// every server of the configuration with its tools or what went wrong, sorted
// by name, a server that fails with the tools it listed last; servers no
// longer named are closed and forgotten; a server down answers its error
// again without a try, and a server that fails joins down, which lives as
// long as its holder: a turn, so it tries each server once; down holds a
// server as it was named, so a new url, key or header tries it again
export async function served(config: ConfigReader, down: Map<string, string>): Promise<Served[]> {
	const all = remotes(config, 'mcp');
	for (const name of connections.keys()) if (!all.some((r) => r.name === name)) drop(name);
	for (const name of kept.keys()) if (!all.some((r) => r.name === name)) kept.delete(name);
	return Promise.all(
		all.map(async (r): Promise<Served> => {
			const signed = signature(r);
			const last = kept.get(r.name);
			const tools = last?.signed === signed ? { tools: last.tools } : {};
			const error = down.get(signed);
			if (error !== undefined) return { server: r.name, error, ...tools };
			try {
				const c = connection(r);
				const listed = await c.tools;
				kept.set(r.name, { signed, tools: listed });
				return { server: r.name, tools: listed, ...system(await c.client) };
			} catch (e) {
				down.set(signed, (e as Error).message);
				const locked = e instanceof SdkHttpError && e.status === UNAUTHORIZED;
				return {
					server: r.name,
					error: (e as Error).message,
					...tools,
					...(locked ? { locked } : {})
				};
			}
		})
	);
}

// a route of a KiSS server: its name, and whether it holds a key
interface Route {
	name: string;
	keyed: boolean;
}

// the routes a server publishes now, none from a server that serves no
// resources
async function routes(remote: Remote): Promise<Route[]> {
	const c = await connection(remote).client;
	if (!c.getServerCapabilities()?.resources) return [];
	const options = { cacheMode: 'refresh' as const, timeout: MCP_CONNECT_TIMEOUT_MS };
	const { resources } = await c.listResources(undefined, options);
	return resources
		.filter((r) => r.uri.startsWith(ROUTE_URI))
		.map((r) => ({
			name: r.uri.slice(ROUTE_URI.length),
			keyed: r._meta?.[ROUTE_KEYED_META] === true
		}));
}

// every item of the module the batch sends through a new proxy finds the
// route of its name on that server, a route with a key when the item holds
// one here, so the server it names keeps taking the requests the item sends
export async function routed(
	module: string,
	config: ConfigReader,
	before: ConfigReader,
	source: ConfigReader
): Promise<string | null> {
	for (const name of config.names(module)) {
		const proxy = config.get(`${module} proxy`, name);
		if (proxy === undefined || proxy === before.get(`${module} proxy`, name)) continue;
		const server = remotes(source, 'mcp').find((r) => r.name === proxy);
		if (!server) continue;
		const route = (await routes(server)).find((r) => r.name === name);
		if (!route)
			return `mcp ${proxy} has no route ${name} -> set proxy url ${name} <url> on mcp ${proxy}`;
		if (!route.keyed && config.get(`${module} key`, name) !== undefined)
			return `${module} ${name} holds a key, route ${name} of mcp ${proxy} holds none -> set proxy key ${name} on mcp ${proxy}`;
	}
	return null;
}

// the servers that take no request without the right key, by name
export async function locked(config: ConfigReader): Promise<string[]> {
	return (await served(config, new Map())).filter((s) => s.locked).map((s) => s.server);
}

// the tools the model sees: the given ones, then those of every server, each
// under its own name, but those the user turned off; a server that fails
// serves the tools it listed last, a name already served stays with the first
// to serve it, and each tells why as a problem the user reads, never stopping
// anything
export async function aggregate(
	config: ConfigReader,
	own: readonly Tool[],
	down: Map<string, string>
): Promise<{ tools: Tool[]; problems: string[] }> {
	const on = (t: Tool) => config.get('tools use', t.name) !== OFF;
	const owner = new Map(own.filter(on).map((t) => [t.name, NAME]));
	const tools = own.filter(on);
	const problems: string[] = [];
	for (const s of await served(config, down)) {
		if (s.error) problems.push(`mcp ${s.server}: ${s.error}`);
		for (const t of (s.tools ?? []).filter(on)) {
			const first = owner.get(t.name);
			if (first) {
				problems.push(`mcp ${s.server}: tool ${t.name} left out, ${first} serves it`);
				continue;
			}
			owner.set(t.name, `mcp ${s.server}`);
			tools.push(t);
		}
	}
	return { tools, problems };
}
