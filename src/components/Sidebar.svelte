<script lang="ts">
	import { app, newChat, open, pin, remove } from '../lib/state.svelte.js';
	import type { Conversation } from '../lib/types.js';
	import { NAME, dayName } from '../lib/config.js';
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
				<button class="title new" onclick={newChat}><Icon name="plus" />New chat</button>
			</div>
		</div>
		{#each groups as g (g.name)}
			<div class="group">{g.name}</div>
			{#each g.conversations as c (c.id)}
				<div class="item" class:active={app.current?.id === c.id}>
					<button class="title" onclick={() => open(c.id)} title={c.title}>{c.title}</button>
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
	onconfirm={() => remove(doomed!.id)}
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
		font-size: var(--size-title);
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
		font-size: var(--size-secondary);
		color: var(--fg-dim);
	}
	.group::first-letter {
		text-transform: uppercase;
	}
	.item {
		display: flex;
		align-items: center;
		border-radius: calc(var(--radius) * 0.6);
	}
	.item:hover {
		background: var(--hover);
	}
	.item.active {
		background: var(--hover-strong);
	}
	/* a title too long fades out instead of losing letters to an ellipsis */
	.title {
		flex: 1;
		min-width: 0;
		text-align: left;
		padding: 0.5rem 0 0.5rem 0.75rem;
		white-space: nowrap;
		overflow: hidden;
		mask-image: linear-gradient(to right, black calc(100% - 1.5rem), transparent);
		font-size: var(--size-secondary);
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
	@media (hover: none) {
		.act {
			display: flex;
		}
	}
</style>
