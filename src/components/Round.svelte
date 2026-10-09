<script lang="ts">
	import type { Attachment } from 'svelte/attachments';
	import type { Call, Round } from '../lib/types.js';
	import Markdown from '../markdown/Markdown.svelte';
	import Icon from './Icon.svelte';
	import { INFERENCE, MARKDOWN, OPEN, display, type Fold } from '../lib/display.svelte.js';

	// live: the round the model writes now
	let { round, live = false }: { round: Round; live?: boolean } = $props();

	// open, or under inference open while the model writes it only
	function unfolded(mode: Fold, writing: boolean): boolean {
		return mode === OPEN || (mode === INFERENCE && writing);
	}

	const thinking = $derived(unfolded(display.thinking, live && !round.text && !round.calls.length));

	// opens or folds a block each time its display setting changes, and only
	// then: a click holds while the content of the block streams
	function fold(open: () => boolean): Attachment<HTMLDetailsElement> {
		return (node) => {
			node.open = open();
		};
	}

	// the arguments of a call, in the order the model wrote them; none while
	// they stream or when they are not a JSON object
	function args(c: Call): [string, unknown][] {
		try {
			const parsed: unknown = JSON.parse(c.args);
			return parsed && typeof parsed === 'object' && !Array.isArray(parsed)
				? Object.entries(parsed)
				: [];
		} catch {
			return [];
		}
	}

	// an argument as it reads: a string as is, anything else as JSON
	function text(value: unknown): string {
		return typeof value === 'string' ? value : JSON.stringify(value);
	}

	// what a folded call shows: the argument the display picks for its tool,
	// else the first one, on its first line
	function preview(c: Call, all: [string, unknown][]): string {
		const picked = all.find(([k]) => k === display.preview[c.name]) ?? all[0];
		return picked ? text(picked[1]).split('\n')[0] : '';
	}
</script>

{#if round.reasoning}
	<details {@attach fold(() => thinking)}>
		<summary><Icon name="chevron" /><span class="head">Thinking</span></summary>
		<div class="body">
			{#if display.render.thinking === MARKDOWN}
				<Markdown text={round.reasoning} />
			{:else}
				<div class="plain">{round.reasoning}</div>
			{/if}
		</div>
	</details>
{/if}
{#if round.text}
	{#if display.render.reply === MARKDOWN}
		<Markdown text={round.text} />
	{:else}
		<div class="plain">{round.text}</div>
	{/if}
{/if}
{#each round.calls as c, i (i)}
	{@const all = args(c)}
	{@const open = unfolded(display.tools, c.ok === undefined)}
	<details
		class="tool"
		class:failed={c.ok === false}
		class:pending={c.ok === undefined}
		{@attach fold(() => open)}
	>
		<summary>
			<Icon name="chevron" />
			<span class="head">{c.name || '...'}</span>
			<span class="preview">{preview(c, all)}</span>
		</summary>
		<div class="body">
			{#if all.length}
				{#each all as [key, value] (key)}
					<div>{key}</div>
					<pre>{text(value)}</pre>
				{/each}
			{:else}
				<pre>{c.args}</pre>
			{/if}
			{#if c.result !== undefined}
				<pre class="output">{c.result}</pre>
			{/if}
			{#each c.images ?? [] as image, j (j)}
				<img src={`data:${image.mime};base64,${image.data}`} alt={`${c.name} result ${j + 1}`} />
			{/each}
		</div>
	</details>
{/each}

<style>
	/* the thinking and the tool calls read like the CLI: small and monospace */
	details {
		color: var(--fg-dim);
		font-family: var(--mono);
		font-size: var(--font-small);
	}
	summary {
		cursor: pointer;
		user-select: none;
		display: flex;
		align-items: center;
		gap: 0.6rem;
		min-width: 0;
	}
	/* the chevron points down while the details are open */
	details[open] > summary > :global(svg) {
		rotate: 90deg;
	}
	summary::-webkit-details-marker {
		display: none;
	}
	.head {
		flex: none;
	}
	.tool .head {
		color: var(--ok);
	}
	.tool.failed .head {
		color: var(--danger);
	}
	.tool.pending .head {
		color: var(--accent-text);
	}
	.preview {
		min-width: 0;
		overflow: hidden;
		white-space: nowrap;
		text-overflow: ellipsis;
	}
	.body {
		padding: 0.4rem 0 0 0.9rem;
		border-left: 2px solid var(--line);
		margin-top: 0.3rem;
	}
	/* a block of text rendered plain reads as written, in monospace */
	.plain {
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		font-family: var(--mono);
	}
	/* the thinking rendered as markdown reads in the font of the page, small,
	   its headings at the size of the block */
	.body > :global(.markdown) {
		font-family: var(--font);
	}
	.body > :global(.markdown :is(h1, h2, h3, h4, h5, h6)) {
		font-size: inherit;
	}
	pre {
		margin: 0 0 0.4rem;
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		font: inherit;
		color: var(--fg);
	}
	.output {
		color: var(--fg-dim);
		border-top: 1px dashed var(--line);
		padding-top: 0.3rem;
	}
	img {
		display: block;
		max-width: 100%;
		margin-top: 0.4rem;
		border-radius: calc(var(--radius) * 0.4);
	}
</style>
