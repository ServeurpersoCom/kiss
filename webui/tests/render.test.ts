import { describe, expect, it } from 'vitest';
import { Renderer } from '../src/markdown/render.js';

// the page a text renders to, streamed or whole
function page(text: string): HTMLElement {
	const view = new Renderer().render(text);
	const root = document.createElement('div');
	root.innerHTML = view.blocks.map((b) => b.html).join('') + view.tail;
	return root;
}

describe('markdown', () => {
	it('shows raw HTML as the text it is, in a line or as a block', () => {
		const root = page('use <div> here\n\n<section>\n  <b>x</b>\n</section>\n\nend');
		expect(root.querySelector('section, b')).toBeNull();
		expect(root.textContent).toContain('use <div> here');
		expect(root.textContent).toContain('\u00a0\u00a0<b>x</b>');
	});

	it('writes the line breaks and the bullet list of a table cell, in a box of its own', () => {
		const root = page('| a | b |\n|---|---|\n| x<br>y | <ul><li>1</li><li>2</li></ul> |');
		const [first, second] = root.querySelectorAll('.table > table td');
		expect(first.innerHTML).toBe('x<br>y');
		expect([...second.querySelectorAll('ul > li')].map((li) => li.textContent)).toEqual(['1', '2']);
	});

	it('heads a code block with its language and a copy button, any language colored', () => {
		const root = page('```nix\n{ a = 1; }\n```\n\ntext');
		const code = root.querySelector('.code')!;
		expect(code.querySelector('.head')!.textContent).toBe('nix');
		expect(code.querySelectorAll('.head .copy svg')).toHaveLength(2);
		expect(code.querySelector('pre code .hljs-attr, pre code [class^="hljs-"]')).not.toBeNull();
	});

	it('heads a code block still streaming the same way', () => {
		const root = page('text\n\n```js\nlet a');
		expect(root.querySelector('.code .head')!.textContent).toBe('js');
		expect(root.querySelector('.code pre')!.textContent).toBe('let a\n');
	});

	it('lets every element read its direction from its own text', () => {
		expect(page('a\n\nb').querySelector('p')!.getAttribute('dir')).toBe('auto');
	});
});
