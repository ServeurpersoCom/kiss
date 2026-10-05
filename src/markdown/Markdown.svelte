<script lang="ts">
	import 'katex/dist/katex.min.css';
	import type { Attachment } from 'svelte/attachments';
	import { Renderer, type View } from './render.js';

	let { text }: { text: string } = $props();

	const renderer = new Renderer();
	let view: View = $state.raw({ blocks: [], tail: '' });
	let frame = 0;

	// one render per animation frame at most, on the latest text
	$effect(() => {
		void text;
		if (frame) return;
		frame = requestAnimationFrame(() => {
			frame = 0;
			view = renderer.render(text);
		});
	});

	$effect(() => () => cancelAnimationFrame(frame));

	// how long a copy button shows its check
	const COPIED_MS = 1500;

	// a copy button puts the code of its block on the clipboard
	const copy: Attachment<HTMLDivElement> = (node) => {
		const onclick = (e: MouseEvent) => {
			const button = (e.target as Element).closest('.copy');
			if (!button) return;
			void navigator.clipboard.writeText(
				button.closest('.code')!.querySelector('pre')!.textContent ?? ''
			);
			button.classList.add('done');
			setTimeout(() => button.classList.remove('done'), COPIED_MS);
		};
		node.addEventListener('click', onclick);
		return () => node.removeEventListener('click', onclick);
	};
</script>

<div class="markdown" {@attach copy}>
	{#each view.blocks as block (block.key)}
		{@html block.html}
	{/each}
	{@html view.tail}
</div>

<style>
	.markdown {
		line-height: 1.6;
		overflow-wrap: anywhere;
	}
	.markdown :global(:first-child) {
		margin-top: 0;
	}
	.markdown :global(:last-child) {
		margin-bottom: 0;
	}
	.markdown :global(.code) {
		margin: 1em 0;
		background: var(--code-bg);
		border-radius: calc(var(--radius) * 0.6);
	}
	/* the language of a block, and its copy button showing a check once done */
	.markdown :global(.head) {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 0.4rem 0.6rem 0 1rem;
		font-family: var(--mono);
		font-size: var(--font-small);
		color: var(--fg-dim);
	}
	.markdown :global(.copy) {
		display: flex;
		padding: 0.2rem;
		font: inherit;
		color: inherit;
		background: none;
		border: none;
		cursor: pointer;
	}
	.markdown :global(.copy:hover) {
		color: var(--fg);
	}
	.markdown :global(.copy svg:last-child),
	.markdown :global(.copy.done svg:first-child) {
		display: none;
	}
	.markdown :global(.copy.done svg:last-child) {
		display: block;
		color: var(--ok);
	}
	.markdown :global(pre) {
		margin: 0;
		padding: 0.5rem 1rem 0.8rem;
		overflow-x: auto;
		font-family: var(--mono);
		font-size: var(--font-small);
		line-height: 1.45;
	}
	.markdown :global(code) {
		font-family: var(--mono);
	}
	/* a heading stands out by its weight, at the size of the text */
	.markdown :global(:is(h1, h2, h3, h4, h5, h6)) {
		font-size: var(--font-large);
	}
	.markdown :global(:not(pre) > code) {
		background: var(--code-bg);
		padding: 0.1em 0.35em;
		border-radius: 0.35em;
	}
	.markdown :global(pre code.hljs) {
		background: none;
		padding: 0;
	}
	.markdown :global(.hljs-comment),
	.markdown :global(.hljs-quote) {
		color: var(--fg-dim);
		font-style: italic;
	}
	.markdown :global(.hljs-keyword),
	.markdown :global(.hljs-selector-tag),
	.markdown :global(.hljs-meta) {
		color: var(--accent-text);
	}
	.markdown :global(.hljs-string),
	.markdown :global(.hljs-regexp),
	.markdown :global(.hljs-addition) {
		color: var(--code-string);
	}
	.markdown :global(.hljs-number),
	.markdown :global(.hljs-literal),
	.markdown :global(.hljs-symbol) {
		color: var(--code-number);
	}
	.markdown :global(.hljs-title),
	.markdown :global(.hljs-section),
	.markdown :global(.hljs-built_in),
	.markdown :global(.hljs-type) {
		color: var(--code-title);
	}
	.markdown :global(.hljs-deletion) {
		color: var(--danger);
	}
	.markdown :global(a) {
		color: var(--accent-text);
	}
	/* a wide table scrolls in its own box */
	.markdown :global(.table) {
		overflow-x: auto;
	}
	.markdown :global(table) {
		border-collapse: collapse;
	}
	.markdown :global(th),
	.markdown :global(td) {
		border: 1px solid var(--line);
		padding: 0.35rem 0.7rem;
	}
	.markdown :global(blockquote) {
		margin: 0;
		padding-left: 1rem;
		border-left: 3px solid var(--accent);
		color: var(--fg-dim);
	}
</style>
