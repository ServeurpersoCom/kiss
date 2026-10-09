<script lang="ts">
	import { app, newChat, open, remove } from '../lib/state.svelte.js';
	import { NAME } from '../lib/config.js';
	import Icon from './Icon.svelte';
</script>

<aside>
	<header>
		<span class="logo">{NAME}</span>
		<button class="new" onclick={newChat}>New chat</button>
	</header>
	<nav>
		{#each app.conversations as c (c.id)}
			<div class="item" class:active={app.current?.id === c.id}>
				<button class="title" onclick={() => open(c.id)} title={c.title}>{c.title}</button>
				<button class="act" onclick={() => remove(c.id)} aria-label="Delete">
					<Icon name="close" />
				</button>
			</div>
		{/each}
	</nav>
</aside>

<style>
	aside {
		display: flex;
		flex-direction: column;
		min-height: 0;
		height: 100%;
		box-sizing: border-box;
		background: var(--sidebar);
		border-right: 1px solid var(--line);
		padding: 0.75rem;
		gap: 0.75rem;
	}
	header {
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
		overflow-y: auto;
		display: flex;
		flex-direction: column;
		gap: 2px;
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
	.act {
		display: flex;
		align-self: stretch;
		align-items: center;
		opacity: 0;
		padding: 0 0.6rem 0 0.4rem;
		color: var(--fg-dim);
	}
	.item:hover .act {
		opacity: 1;
	}
</style>
