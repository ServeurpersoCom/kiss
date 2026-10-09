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
	<header>
		<span class="logo">{NAME}</span>
		<button class="new" onclick={newChat}>New chat</button>
	</header>
	<nav>
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
		gap: 0.75rem;
	}
	/* the head and the list keep the gutter inside them, so the scrollbar of
	   the list runs against the border, down to the bottom of the page */
	header {
		padding: 0 0.75rem;
		display: flex;
		align-items: center;
		justify-content: space-between;
	}
	.logo {
		font-weight: 700;
		font-size: var(--size-title);
		color: var(--accent-text);
		padding-left: 0.4rem;
	}
	button {
		font: inherit;
		color: inherit;
		background: none;
		border: none;
		cursor: pointer;
	}
	.new {
		background: var(--accent);
		color: var(--on-accent);
		padding: 0.4rem 0.9rem;
		border-radius: 999px;
		font-size: var(--size-secondary);
	}
	nav {
		padding: 0 0.75rem 0.75rem;
		overflow-y: auto;
		display: flex;
		flex-direction: column;
		gap: 2px;
	}
	/* the name of a group over its conversations, pinned or of a day, its
	   first letter capital */
	.group {
		padding: 0.9rem 0.6rem 0.3rem;
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
	.title {
		flex: 1;
		min-width: 0;
		text-align: left;
		padding: 0.5rem 0.6rem;
		white-space: nowrap;
		overflow: hidden;
		text-overflow: ellipsis;
		font-size: var(--size-secondary);
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
		padding-right: 0.6rem;
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
