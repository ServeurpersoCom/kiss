import {
	Client,
	SdkHttpError,
	StreamableHTTPClientTransport,
	isInputRequiredResult
} from '@modelcontextprotocol/client';
import type {
	CallToolRequest,
	CallToolResult,
	InputRequest,
	InputRequiredResult
} from '@modelcontextprotocol/client';
import type { ConfigReader, Image, Outcome, Tool, ToolContext } from './types.js';
import { MCP_CONNECT_TIMEOUT_MS, NAME, SYSTEM, capital } from './config.js';
import { headers, remotes, type Remote } from './remote.js';
import { OFF } from './tools.js';
import { learn } from './secrets.js';

// the MCP servers of the configuration, over Streamable HTTP: one client per
// server, kept while its url and key stay, with the tool list it serves, asked
// again as each turn starts, so a tool a server gains shows from the next turn
// on and the tools the model sees stay the same from round to round; a
// request that fails drops the client, and the next use connects again

// a client, its tool list, and whether that list came
interface Connection {
	remote: Remote;
	tools: Promise<Tool[]>;
	client: Promise<Client>;
	settled: boolean;
}

const connections = new Map<string, Connection>();

// the HTTP status of a server that takes no request without the right key
const UNAUTHORIZED = 401;

// the one input a server may ask of KiSS: a form whose fields are secrets,
// text each typed in the masked field of the page, or yes or no, the user
// confirming what the server says; the user answers them all, or leaving one
// cancels the form
const ELICITATION = 'elicitation/create';
const FORM_MODE = 'form';
const TEXT_FIELD = 'string';
const YES_FIELD = 'boolean';
const ACTIONS = ['accept', 'cancel'] as const;
const [ACCEPT, CANCEL] = ACTIONS;

// what the server answered, or did not; a KiSS server also gives the line it
// writes of itself, server forge 5633ee7 (2026-10-11) on linux; locked, it
// answered that it takes no request without the right key
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

// what the user answers to a form of a server under its message: a text field
// typed out of the sight of the model, a yes or no field confirmed; an input
// other than a form of such fields is none KiSS answers
async function answer(ctx: ToolContext, server: string, input: InputRequest): Promise<object> {
	const params = (input.params ?? {}) as {
		mode?: string;
		message?: string;
		requestedSchema?: { properties?: Record<string, { type?: string }> };
	};
	if (input.method !== ELICITATION || (params.mode ?? FORM_MODE) !== FORM_MODE) {
		throw new Error(`asks for ${input.method} ${params.mode ?? ''}, which KiSS does not answer`);
	}
	const fields = Object.entries(params.requestedSchema?.properties ?? {});
	if (!fields.length) throw new Error('asks for a form with no field');
	const other = fields.find(
		([, schema]) => schema.type !== TEXT_FIELD && schema.type !== YES_FIELD
	);
	if (other)
		throw new Error(`asks for ${other[0]}, a field KiSS does not answer: only text or yes`);
	const content: Record<string, string | boolean> = {};
	for (const [field, schema] of fields) {
		const note = params.message ?? '';
		if (schema.type === YES_FIELD) {
			if (!(await ctx.confirm(note || `${field} of ${server}?`))) return { action: CANCEL };
			content[field] = true;
			continue;
		}
		const value = await ctx.secret(`${field} of ${server}`, note);
		if (value === null) return { action: CANCEL };
		learn(value);
		content[field] = value;
	}
	return { action: ACCEPT, content };
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
		if (!inputs.length) throw new Error('asks to be called again for nothing');
		const inputResponses: Record<string, object> = {};
		for (const [key, input] of inputs) {
			ctx.signal.throwIfAborted();
			inputResponses[key] = await answer(ctx, remote.name, input);
		}
		const again = { ...params, inputResponses, requestState: result.requestState };
		result = await client.callTool(again as CallToolRequest['params'], options);
	}
	return outcome(result);
}

// a server as the configuration names it: its name, url, key, headers and
// timeout; a change of any makes it another server
const signature = (remote: Remote): string => JSON.stringify(remote);

// the tools a client lists now, each called through that client
async function listed(remote: Remote, c: Client): Promise<Tool[]> {
	const { tools } = await c.listTools(undefined, { timeout: MCP_CONNECT_TIMEOUT_MS });
	return tools.map((t): Tool => ({
		name: t.name,
		description: t.description ?? '',
		parameters: t.inputSchema,
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
	if (held && signature(held.remote) === signature(remote)) return held;
	if (held) drop(remote.name);
	const client = connect(remote);
	const made: Connection = {
		remote,
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
// by name; servers no longer named are closed; a server down answers its
// error again without a try, and a server that fails joins down, which lives
// as long as its holder: a turn, so it tries each server once; down holds a
// server as it was named, so a new url, key or header tries it again
export async function served(config: ConfigReader, down: Map<string, string>): Promise<Served[]> {
	const all = remotes(config, 'mcp');
	for (const name of connections.keys()) if (!all.some((r) => r.name === name)) drop(name);
	return Promise.all(
		all.map(async (r): Promise<Served> => {
			const error = down.get(signature(r));
			if (error !== undefined) return { server: r.name, error };
			try {
				const c = connection(r);
				const tools = await c.tools;
				return { server: r.name, tools, ...system(await c.client) };
			} catch (e) {
				down.set(signature(r), (e as Error).message);
				const locked = e instanceof SdkHttpError && e.status === UNAUTHORIZED;
				return { server: r.name, error: (e as Error).message, ...(locked ? { locked } : {}) };
			}
		})
	);
}

// the servers that take no request without the right key, by name
export async function locked(config: ConfigReader): Promise<string[]> {
	return (await served(config, new Map())).filter((s) => s.locked).map((s) => s.server);
}

// the tools the model sees: the given ones, then those of every server, each
// under its own name, but those the user turned off; a server that fails
// serves nothing, a name already served stays with the first to serve it, and
// each tells why as a problem, never stopping anything
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
		if (s.error) {
			problems.push(`mcp ${s.server}: ${s.error}`);
			continue;
		}
		for (const t of s.tools!.filter(on)) {
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
