<script lang="ts">
	import 'katex/dist/katex.min.css';
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
</script>

<div class="markdown">
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
	.markdown :global(pre) {
		background: var(--code-bg);
		border-radius: calc(var(--radius) * 0.6);
		padding: 0.8rem 1rem;
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
	.markdown :global(table) {
		border-collapse: collapse;
		display: block;
		overflow-x: auto;
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
