<script lang="ts">
	import { app, newChat, open, pin, remove } from '../lib/state.svelte.js';
	import type { Conversation } from '../lib/types.js';
	import { CLI, NAME, dayName } from '../lib/config.js';
	import { clock } from '../lib/clock.svelte.js';
	import Icon from './Icon.svelte';
	import Dialog from './Dialog.svelte';

	const PINNED = 'Pinned';

	// the conversations, the last changed first: the pinned ones, then the others
	// by the day they last changed
	const groups = $derived.by(() => {
		const sorted = [...app.conversations].sort((a, b) => b.updated - a.updated);
		const pinned = sorted.filter((c) => c.pinned);
		const out = pinned.length ? [{ name: PINNED, conversations: pinned }] : [];
		for (const c of sorted.filter((c) => !c.pinned)) {
			const name = dayName(c.updated, clock.now);
			if (out.at(-1)?.name === name) out.at(-1)!.conversations.push(c);
			else out.push({ name, conversations: [c] });
		}
		return out;
	});

	// the conversation whose delete waits for the user to confirm it
	let doomed: Conversation | null = $state(null);
</script>

<aside>
	<header>{NAME}</header>
	<nav>
		<div class="top">
			<div class="item" class:active={!app.current}>
				<button class="new" onclick={newChat}><Icon name="plus" />New chat</button>
			</div>
		</div>
		{#each groups as g (g.name)}
			<div class="group">{g.name}</div>
			{#each g.conversations as c (c.id)}
				<div class="item" class:active={app.current?.id === c.id}>
					<button class="title" onclick={() => open(c.id)} title={c.title || CLI}
						>{c.title || CLI}</button
					>
					{#if c.id in app.replies}
						<span class="busy" aria-label="Answering"></span>
					{/if}
					<button class="act" onclick={() => pin(c.id)} aria-label={c.pinned ? 'Unpin' : 'Pin'}>
						<Icon name="pin" />
					</button>
					<button class="act" onclick={() => (doomed = c)} aria-label="Delete">
						<Icon name="close" />
					</button>
				</div>
			{/each}
		{/each}
	</nav>
</aside>

<Dialog
	bind:open={() => doomed !== null, (open) => !open && (doomed = null)}
	title="Delete this conversation?"
	onconfirm={() => remove([doomed!.id])}
/>

<style>
	aside {
		display: flex;
		flex-direction: column;
		min-height: 0;
		height: 100%;
		box-sizing: border-box;
		background: var(--sidebar);
		border-right: 1px solid var(--line);
		padding-top: 0.75rem;
		gap: 0.5rem;
	}
	/* every text of the sidebar starts at one inset from its left edge */
	header {
		padding: 0 0.75rem;
		font-weight: 700;
		font-size: var(--text-title);
		color: var(--accent-text);
	}
	button {
		font: inherit;
		color: inherit;
		background: none;
		border: none;
		cursor: pointer;
	}
	/* the list runs from the left edge of the page to its own scrollbar, against
	   the border, and down to the bottom of the page */
	nav {
		padding-bottom: 0.75rem;
		overflow-y: auto;
		display: flex;
		flex-direction: column;
		gap: 2px;
	}
	/* New chat stays at the top of the list while it scrolls, over what passes
	   under it */
	.top {
		position: sticky;
		top: 0;
		z-index: 1;
		background: var(--sidebar);
	}
	/* the name of a group over its conversations, pinned or of a day, its
	   first letter capital */
	.group {
		padding: 0.9rem 0.75rem 0.3rem;
		font-size: var(--text-secondary);
		color: var(--fg-dim);
	}
	.group::first-letter {
		text-transform: uppercase;
	}
	/* an item paints its background from --item-bg, which follows its state,
	   so what lies over its text can fade into it; rounded as a bubble, its
	   corners clip what it holds, the fade of a title reaching its right edge
	   included, and it keeps its height in the list, which never squeezes it */
	.item {
		--item-bg: var(--sidebar);
		flex: none;
		display: flex;
		align-items: center;
		border-radius: var(--radius);
		overflow: hidden;
		background: var(--item-bg);
	}
	.item:hover {
		--item-bg: var(--hover);
	}
	.item.active {
		--item-bg: var(--hover-strong);
	}
	.title,
	.new {
		flex: 1;
		min-width: 0;
		text-align: left;
		padding: 0.5rem 0 0.5rem 0.75rem;
		white-space: nowrap;
		overflow: hidden;
		font-size: var(--text-primary);
	}
	/* a title too long fades out instead of losing letters to an ellipsis: a
	   layer over its right end goes from clear to the background of the item,
	   and the letters under it stay text painted by its color like every other */
	.title {
		position: relative;
	}
	.title::after {
		content: '';
		position: absolute;
		top: 0;
		right: 0;
		bottom: 0;
		width: 1.5rem;
		background: linear-gradient(to right, transparent, var(--item-bg));
	}
	.new {
		display: flex;
		align-items: center;
		gap: 0.5rem;
	}
	/* the pin and the cross show while the item is hovered, the title taking
	   the whole width otherwise; always where nothing hovers */
	.act {
		display: none;
		align-self: stretch;
		align-items: center;
		padding: 0 0.3rem;
		color: var(--fg-dim);
	}
	.act:last-child {
		padding-right: 0.75rem;
	}
	.item:hover .act {
		display: flex;
	}
	/* a dot of the accent while the model answers in the conversation, giving
	   way to the pin and the cross on hover */
	.busy {
		flex: none;
		width: 0.5rem;
		height: 0.5rem;
		margin: 0 0.75rem 0 0.5rem;
		border-radius: 50%;
		background: var(--accent);
	}
	.item:hover .busy {
		display: none;
	}
	@media (hover: none) {
		.act {
			display: flex;
		}
	}
</style>
