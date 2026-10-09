<script lang="ts">
	import {
		FILE_EXTENSION,
		app,
		download,
		newChat,
		open,
		remove,
		upload
	} from '../lib/state.svelte.js';
	import { NAME } from '../lib/config.js';
	import Icon from './Icon.svelte';

	let picker: HTMLInputElement;
	// why the last file picked imported nothing
	let failure = $state('');

	async function onchange() {
		const file = picker.files?.[0];
		picker.value = '';
		failure = '';
		if (!file) return;
		await upload(file).catch((e: Error) => (failure = `${file.name}: ${e.message}`));
	}
</script>

<aside>
	<header>
		<span class="logo">{NAME}</span>
		<button class="import" onclick={() => picker.click()} aria-label="Import a conversation">
			<Icon name="upload" />
		</button>
		<button class="new" onclick={newChat}>New chat</button>
		<input bind:this={picker} type="file" accept={FILE_EXTENSION} {onchange} hidden />
	</header>
	{#if failure}
		<p class="failure">{failure}</p>
	{/if}
	<nav>
		{#each app.conversations as c (c.id)}
			<div class="item" class:active={app.current?.id === c.id}>
				<button class="title" onclick={() => open(c.id)} title={c.title}>{c.title}</button>
				<button class="act" onclick={() => download(c)} aria-label="Export">
					<Icon name="download" />
				</button>
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
		gap: 0.5rem;
	}
	.logo {
		flex: 1;
		font-weight: 700;
		font-size: var(--font-name);
		color: var(--accent-text);
		padding-left: 0.4rem;
	}
	.import {
		display: flex;
		padding: 0.2rem;
		color: var(--fg-dim);
	}
	.import:hover {
		color: var(--fg);
	}
	.failure {
		margin: 0;
		font-size: var(--font-small);
		color: var(--danger);
		overflow-wrap: anywhere;
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
		font-size: var(--font-small);
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
		font-size: var(--font-small);
	}
	.act {
		display: flex;
		align-self: stretch;
		align-items: center;
		opacity: 0;
		padding: 0 0.4rem;
		color: var(--fg-dim);
	}
	.act:last-child {
		padding-right: 0.6rem;
	}
	.item:hover .act {
		opacity: 1;
	}
</style>
