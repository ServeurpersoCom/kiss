import { Client, StreamableHTTPClientTransport } from '@modelcontextprotocol/client';
import type { CallToolResult } from '@modelcontextprotocol/client';
import type { ConfigReader, Image, Outcome, Tool } from './types.js';
import { MCP_CALL_TIMEOUT_MS, MCP_CONNECT_TIMEOUT_MS, NAME } from './config.js';
import { bearer, remotes, type Remote } from './remote.js';

// the MCP servers of the configuration, over Streamable HTTP: one client per
// server, kept while its url and key stay, with the tool list it served once
// connected, so the tools the model sees stay the same from turn to turn; a
// request that fails drops the client, and the next use connects again

interface Connection {
	remote: Remote;
	tools: Promise<Tool[]>;
	client: Promise<Client>;
}

const connections = new Map<string, Connection>();

// what the server answered, or did not
export interface Served {
	server: string;
	tools?: Tool[];
	error?: string;
}

function drop(name: string): void {
	const c = connections.get(name);
	connections.delete(name);
	void c?.client.then((client) => client.close()).catch(() => undefined);
}

// a 2026 server speaks its own era, a 2025 one answers the initialize handshake
function connect(remote: Remote): Promise<Client> {
	const client = new Client(
		{ name: 'kiss', title: NAME, version: __KISS_VERSION__ },
		{ versionNegotiation: { mode: 'auto' } }
	);
	const transport = new StreamableHTTPClientTransport(new URL(remote.url), {
		requestInit: { headers: bearer(remote.key) }
	});
	return client.connect(transport, { timeout: MCP_CONNECT_TIMEOUT_MS }).then(() => client);
}

// the size of the data base64 encodes
function bytes(base64: string): number {
	const pad = base64.endsWith('==') ? 2 : base64.endsWith('=') ? 1 : 0;
	return (base64.length / 4) * 3 - pad;
}

// the content blocks as the model reads them: text as is, every other block a
// line naming it; images also go to the user
function outcome(result: CallToolResult): Outcome {
	const lines: string[] = [];
	const images: Image[] = [];
	for (const block of result.content) {
		if (block.type === 'text') lines.push(block.text);
		else if (block.type === 'image') {
			images.push({ mime: block.mimeType, data: block.data });
			lines.push(`! image ${block.mimeType}, ${bytes(block.data)} bytes`);
		} else lines.push(`! ${block.type} content`);
	}
	return { ok: !result.isError, text: lines.join('\n'), ...(images.length ? { images } : {}) };
}

function connection(remote: Remote): Connection {
	const held = connections.get(remote.name);
	if (held && held.remote.url === remote.url && held.remote.key === remote.key) return held;
	if (held) drop(remote.name);
	const client = connect(remote);
	const tools = client.then(async (c) => {
		const { tools } = await c.listTools(undefined, { timeout: MCP_CONNECT_TIMEOUT_MS });
		return tools.map((t): Tool => ({
			name: t.name,
			description: t.description ?? '',
			parameters: t.inputSchema,
			async run(ctx, args) {
				try {
					return outcome(
						await c.callTool(
							{ name: t.name, arguments: args },
							{ signal: ctx.signal, timeout: MCP_CALL_TIMEOUT_MS }
						)
					);
				} catch (e) {
					if (ctx.signal.aborted) throw e;
					drop(remote.name);
					throw new Error(`mcp ${remote.name}: ${(e as Error).message}`);
				}
			}
		}));
	});
	const made: Connection = { remote, client, tools };
	connections.set(remote.name, made);
	tools.catch(() => connections.get(remote.name) === made && drop(remote.name));
	return made;
}

// every server of the configuration with its tools or what went wrong, sorted
// by name; servers no longer named are closed
export async function served(config: ConfigReader): Promise<Served[]> {
	const all = remotes(config, 'mcp');
	for (const name of connections.keys()) if (!all.some((r) => r.name === name)) drop(name);
	return Promise.all(
		all.map((r) =>
			connection(r).tools.then(
				(tools) => ({ server: r.name, tools }),
				(e: Error) => ({ server: r.name, error: e.message })
			)
		)
	);
}

// the tools the model sees: the given ones, then those of every server, each
// under its own name, but those the user turned off; a server that fails
// serves nothing, a name already served stays with the first to serve it, and
// each tells why as a problem, never stopping anything
export async function aggregate(
	config: ConfigReader,
	own: readonly Tool[]
): Promise<{ tools: Tool[]; problems: string[] }> {
	const on = (t: Tool) => config.get('tools use', t.name) !== 'off';
	const owner = new Map(own.filter(on).map((t) => [t.name, NAME]));
	const tools = own.filter(on);
	const problems: string[] = [];
	for (const s of await served(config)) {
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
