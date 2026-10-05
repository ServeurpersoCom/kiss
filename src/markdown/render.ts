import type { Element, ElementContent, Root as HastRoot, RootContent as HastNode } from 'hast';
import type { PhrasingContent, Root as MdastRoot, RootContent as MdastNode } from 'mdast';
import { all } from 'lowlight';
import { remark } from 'remark';
import remarkGfm from 'remark-gfm';
import remarkMath from 'remark-math';
import remarkBreaks from 'remark-breaks';
import remarkRehype from 'remark-rehype';
import rehypeKatex from 'rehype-katex';
import rehypeHighlight from 'rehype-highlight';
import rehypeStringify from 'rehype-stringify';
import { PATHS, stroke, type IconName } from '../lib/icons.js';

const FENCE = /^ {0,3}(`{3,}|~{3,})(.*)$/;
const LINK_PROTOCOLS = ['http:', 'https:', 'mailto:'];
// the nodes whose children are phrasing, where raw HTML stays in its line
const PHRASING = new Set([
	'paragraph',
	'heading',
	'emphasis',
	'strong',
	'delete',
	'link',
	'linkReference',
	'tableCell'
]);
const BR = /<br\s*\/?\s*>|\n/i;
const LIST = /^<ul>([\s\S]*)<\/ul>$/i;
const ITEM = /<li>([\s\S]*?)<\/li>/gi;
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

function element(tagName: string, children: ElementContent[] = [], className?: string): Element {
	return {
		type: 'element',
		tagName,
		properties: className ? { className: [className] } : {},
		children
	};
}

function icon(name: IconName): Element {
	return {
		type: 'element',
		tagName: 'svg',
		properties: stroke(name),
		children: [{ ...element('path'), properties: { d: PATHS[name] } }]
	};
}

// raw HTML in the text shows as the text it is, never as markup: in a line it
// stays in its line, a block of it becomes a paragraph keeping its lines and
// their indentation
function remarkLiteral() {
	const lines = (value: string): PhrasingContent[] =>
		value.split(/\r?\n/).flatMap((line, i): PhrasingContent[] => [
			...(i ? [{ type: 'break' } as const] : []),
			{
				type: 'text',
				value: line.replace(/^[ \t]+/, (indent) =>
					indent.replace(/\t/g, '    ').replace(/ /g, '\u00a0')
				)
			}
		]);
	const walk = (node: MdastRoot | MdastNode) => {
		if (!('children' in node)) return;
		const children = (node.children as MdastNode[]).flatMap((child): MdastNode[] => {
			if (child.type !== 'html') {
				walk(child);
				return [child];
			}
			if (PHRASING.has(node.type)) return lines(child.value);
			return [{ type: 'paragraph', children: lines(child.value) }];
		});
		(node as { children: MdastNode[] }).children = children;
	};
	return (tree: MdastRoot) => walk(tree);
}

// the line breaks and the bullet list of a table cell, which markdown cannot
// write there, as the HTML that writes them
function restore(text: string): ElementContent[] {
	const list = LIST.exec(text.trim());
	if (list && !list[1].replace(ITEM, '').trim()) {
		const items = [...list[1].matchAll(ITEM)].map((m) => element('li', restore(m[1])));
		if (items.length) return [element('ul', items)];
	}
	return text
		.split(BR)
		.flatMap((part, i): ElementContent[] => [
			...(i ? [element('br')] : []),
			...(part ? [{ type: 'text', value: part } as const] : [])
		]);
}

function rehypeCells() {
	const walk = (node: HastRoot | HastNode) => {
		if (!('children' in node)) return;
		if (node.type === 'element' && (node.tagName === 'td' || node.tagName === 'th')) {
			const children: ElementContent[] = [];
			let run = '';
			const flush = () => {
				if (run) children.push(...restore(run));
				run = '';
			};
			for (const child of node.children) {
				if (child.type === 'text') run += child.value;
				else if (child.type === 'element' && child.tagName === 'br') run += '\n';
				else {
					flush();
					children.push(child);
				}
			}
			flush();
			node.children = children;
			return;
		}
		node.children.forEach(walk);
	};
	return (tree: HastRoot) => walk(tree);
}

// a wide table scrolls in its own box instead of widening the thread; a code
// block gets a head naming its language, with a button copying it
function rehypeBlocks() {
	const walk = (node: HastRoot | HastNode) => {
		if (!('children' in node)) return;
		node.children = node.children.map((child) => {
			if (child.type !== 'element') return child;
			if (child.tagName === 'table') return element('div', [child], 'table');
			if (child.tagName === 'pre') {
				const code = child.children[0] as Element | undefined;
				const classes = (code?.properties.className ?? []) as string[];
				const lang = classes.find((c) => c.startsWith('language-'))?.slice(9) ?? 'text';
				const copy: Element = {
					type: 'element',
					tagName: 'button',
					properties: { type: 'button', className: ['copy'], ariaLabel: 'Copy' },
					children: [icon('copy'), icon('check')]
				};
				const head = element('div', [{ type: 'text', value: lang }, copy], 'head');
				return element('div', [head, child], 'code');
			}
			walk(child);
			return child;
		}) as typeof node.children;
	};
	return (tree: HastRoot) => walk(tree);
}

// every element holding something reads its direction from its own text
function rehypeDirection() {
	const walk = (node: HastRoot | HastNode) => {
		if (!('children' in node)) return;
		if (node.type === 'element' && node.children.length) node.properties.dir = 'auto';
		node.children.forEach(walk);
	};
	return (tree: HastRoot) => walk(tree);
}

const processor = remark()
	.use(remarkGfm)
	.use(remarkMath)
	.use(remarkBreaks)
	.use(remarkLiteral)
	.use(remarkRehype)
	.use(rehypeKatex)
	.use(rehypeHighlight, { languages: all, aliases: { xml: ['svelte', 'vue'] } })
	.use(rehypeCells)
	.use(rehypeLinks)
	.use(rehypeBlocks)
	.use(rehypeDirection)
	.use(rehypeStringify);

function html(nodes: MdastNode[]): string {
	const tree = processor.runSync({ type: 'root', children: nodes } as MdastRoot);
	return processor.stringify(tree as HastRoot);
}

// where a node sits in the source
function span(node: MdastNode): string {
	return `${node.type}-${node.position?.start.offset}-${node.position?.end.offset}`;
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

// the code fence still open at the end of a streamed text, if any, with the
// language its info names
function openFence(markdown: string): { start: number; lang: string; code: string } | null {
	let fence = '';
	let lang = '';
	let start = 0;
	let offset = 0;
	for (const line of markdown.split('\n')) {
		const m = FENCE.exec(line);
		if (m && !fence) {
			fence = m[1];
			lang = m[2].trim().split(/\s/)[0];
			start = offset;
		} else if (m && line.trim().startsWith(fence) && !m[2].trim()) {
			fence = '';
		}
		offset += line.length + 1;
	}
	if (!fence) return null;
	const body = markdown.slice(start);
	return { start, lang, code: body.slice(body.indexOf('\n') + 1) };
}

// incremental rendering of a growing text: every top level block but the last
// is cached by its source span and the link definitions of the text while the
// text only grows, each block rendering with those definitions; the last block
// renders again on each call and an open code fence shows as the block it
// becomes; a text
// holding footnotes renders whole, its notes numbered across all of it
export class Renderer {
	private previous = '';
	private cache = new Map<string, string>();

	render(source: string): View {
		const markdown = latex(source);
		if (!markdown.startsWith(this.previous)) this.cache.clear();
		this.previous = markdown;
		const open = openFence(markdown);
		const fence = open ? html([{ type: 'code', lang: open.lang || null, value: open.code }]) : '';
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
