import type { Element, Root as HastRoot, RootContent as HastNode } from 'hast';
import type { Root as MdastRoot, RootContent as MdastNode } from 'mdast';
import { remark } from 'remark';
import remarkGfm from 'remark-gfm';
import remarkMath from 'remark-math';
import remarkBreaks from 'remark-breaks';
import remarkRehype from 'remark-rehype';
import rehypeKatex from 'rehype-katex';
import rehypeHighlight from 'rehype-highlight';
import rehypeStringify from 'rehype-stringify';

const FENCE = /^ {0,3}(`{3,}|~{3,})(.*)$/;
const LINK_PROTOCOLS = ['http:', 'https:', 'mailto:'];
// code spans and fenced blocks, where LaTeX delimiters stay as written
const CODE = /(^ {0,3}(`{3,}|~{3,})[\s\S]*?(?:^ {0,3}\2[ \t]*$|$(?![\s\S]))|`[^`\n]*`)/gm;

export interface Block {
	key: string;
	html: string;
}

export interface View {
	blocks: Block[];
	tail: string;
}

// the protocol of a link, relative links reading as the page's own
function protocol(href: string): string {
	try {
		return new URL(href, location.href).protocol;
	} catch {
		return '';
	}
}

// every link opens in a new tab, and a link to anything but a web page or a
// mail address keeps its text only
function rehypeLinks() {
	const walk = (node: HastRoot | HastNode) => {
		if (node.type === 'element' && node.tagName === 'a') {
			const properties = (node as Element).properties;
			if (!LINK_PROTOCOLS.includes(protocol(String(properties.href ?? '')))) {
				delete properties.href;
			}
			properties.target = '_blank';
			properties.rel = ['noopener', 'noreferrer'];
		}
		if ('children' in node) node.children.forEach(walk);
	};
	return (tree: HastRoot) => walk(tree);
}

const processor = remark()
	.use(remarkGfm)
	.use(remarkMath)
	.use(remarkBreaks)
	.use(remarkRehype)
	.use(rehypeKatex)
	.use(rehypeHighlight)
	.use(rehypeLinks)
	.use(rehypeStringify);

function html(nodes: MdastNode[]): string {
	const tree = processor.runSync({ type: 'root', children: nodes } as MdastRoot);
	return processor.stringify(tree as HastRoot);
}

// where a node sits in the source
function span(node: MdastNode): string {
	return `${node.type}-${node.position?.start.offset}-${node.position?.end.offset}`;
}

function escape(text: string): string {
	return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// \( \) and \[ \] become the $ and $$ remark math reads, outside code; the
// split yields both groups of CODE, the code then its fence marker, which the
// code already holds
function latex(markdown: string): string {
	return markdown
		.split(CODE)
		.map((part, i) =>
			i % 3 === 0
				? part.replace(/\\\[([\s\S]*?)\\\]/g, '$$$$$1$$$$').replace(/\\\((.*?)\\\)/g, '$$$1$$')
				: i % 3 === 1
					? part
					: ''
		)
		.join('');
}

// the code fence still open at the end of a streamed text, if any
function openFence(markdown: string): { start: number; code: string } | null {
	let fence = '';
	let start = 0;
	let offset = 0;
	for (const line of markdown.split('\n')) {
		const m = FENCE.exec(line);
		if (m && !fence) {
			fence = m[1];
			start = offset;
		} else if (m && line.trim().startsWith(fence) && !m[2].trim()) {
			fence = '';
		}
		offset += line.length + 1;
	}
	if (!fence) return null;
	const body = markdown.slice(start);
	return { start, code: body.slice(body.indexOf('\n') + 1) };
}

// incremental rendering of a growing text: every top level block but the last
// is cached by its source span and the link definitions of the text while the
// text only grows, each block rendering with those definitions; the last block
// renders again on each call and an open code fence shows as raw code; a text
// holding footnotes renders whole, its notes numbered across all of it
export class Renderer {
	private previous = '';
	private cache = new Map<string, string>();

	render(source: string): View {
		const markdown = latex(source);
		if (!markdown.startsWith(this.previous)) this.cache.clear();
		this.previous = markdown;
		const open = openFence(markdown);
		const fence = open ? `<pre><code>${escape(open.code)}</code></pre>` : '';
		const nodes = processor.parse(open ? markdown.slice(0, open.start) : markdown).children;
		if (nodes.some((n) => n.type === 'footnoteDefinition')) {
			return { blocks: [], tail: html(nodes) + fence };
		}
		const definitions = nodes.filter((n) => n.type === 'definition');
		const context = definitions.map(span).join(' ');
		const stable = open ? nodes.length : Math.max(nodes.length - 1, 0);
		const blocks = nodes.slice(0, stable).map((node) => {
			const key = `${span(node)} ${context}`;
			let cached = this.cache.get(key);
			if (cached === undefined) {
				cached = html([node, ...definitions]);
				this.cache.set(key, cached);
			}
			return { key, html: cached };
		});
		const tail = fence || (nodes.length > stable ? html([nodes[stable], ...definitions]) : '');
		return { blocks, tail };
	}
}
