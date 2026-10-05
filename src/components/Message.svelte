<script lang="ts">
	import type { Message } from '../lib/types.js';
	import Round from './Round.svelte';

	// live: the turn the model writes now
	let { message, live = false }: { message: Message; live?: boolean } = $props();
</script>

{#if message.role === 'user'}
	<div class="user">{message.text}</div>
{:else if message.role === 'cli'}
	<pre class="cli" class:failed={!message.ok}><span>/{message.input}</span>{#if message.output}<br
			/>{message.output}{/if}</pre>
{:else}
	<div class="turn">
		{#each message.rounds as round, i (i)}
			<Round {round} live={live && i === message.rounds.length - 1} />
		{/each}
		{#if message.error}
			<div class="error">{message.error}</div>
		{/if}
	</div>
{/if}

<style>
	.user {
		align-self: flex-end;
		max-width: var(--bubble-width);
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		background: var(--bubble);
		padding: 0.6rem 1rem;
		border-radius: var(--radius);
	}
	.cli {
		font-family: var(--mono);
		font-size: var(--font-small);
		margin: 0;
		padding: 0.5rem 0.8rem;
		background: var(--code-bg);
		border-radius: calc(var(--radius) * 0.6);
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		color: var(--fg-dim);
	}
	.cli span {
		color: var(--accent-text);
	}
	.cli.failed {
		color: var(--danger);
	}
	.turn {
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
	}
	.error {
		color: var(--danger);
		font-size: var(--font-small);
	}
</style>
