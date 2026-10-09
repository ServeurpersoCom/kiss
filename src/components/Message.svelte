<script lang="ts">
	import type { Message } from '../lib/types.js';
	import Round from './Round.svelte';
	import Icon from './Icon.svelte';
	import { COPIED_MS, SLASH } from '../lib/config.js';
	import { source } from '../lib/conversation.js';
	import { app, edit } from '../lib/state.svelte.js';

	// index: where the message sits in its conversation; live: the turn the
	// model writes now
	let {
		message,
		index,
		live = false
	}: { message: Message; index: number; live?: boolean } = $props();

	let copied = $state(false);
	// the text being edited, none while the message shows as sent
	let draft: string | null = $state(null);
	let field: HTMLTextAreaElement | undefined = $state();

	function copy() {
		void navigator.clipboard.writeText(source(message));
		copied = true;
		setTimeout(() => (copied = false), COPIED_MS);
	}

	function begin() {
		if (message.role === 'user') draft = message.text;
	}

	// the field as tall as its lines
	function resize() {
		if (!field) return;
		field.style.height = 'auto';
		field.style.height = field.scrollHeight + 'px';
	}

	$effect(() => {
		if (!field) return;
		resize();
		field.focus();
	});

	function onkeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') draft = null;
		else if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
			e.preventDefault();
			const text = draft?.trim();
			if (!text || app.reply) return;
			draft = null;
			void edit(index, text);
		}
	}
</script>

<div class="message {message.role}">
	{#if message.role === 'user'}
		{#if draft === null}
			<div class="bubble">{message.text}</div>
		{:else}
			<textarea
				class="bubble"
				bind:this={field}
				bind:value={draft}
				oninput={resize}
				{onkeydown}
				rows="1"
			></textarea>
		{/if}
	{:else if message.role === 'cli'}
		<pre class="command" class:failed={!message.ok}><span>{SLASH}{message.input}</span
			>{#if message.output}<br />{message.output}{/if}</pre>
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
	{#if !live && draft === null}
		<div class="actions">
			<button onclick={copy} class:copied aria-label="Copy">
				<Icon name={copied ? 'check' : 'copy'} />
			</button>
			{#if message.role === 'user'}
				<button onclick={begin} disabled={!!app.reply} aria-label="Edit">
					<Icon name="edit" />
				</button>
			{/if}
		</div>
	{/if}
</div>

<style>
	.message {
		display: flex;
		flex-direction: column;
		gap: 0.3rem;
	}
	/* the user writes on the right, in a bubble */
	.user {
		align-self: flex-end;
		align-items: flex-end;
		max-width: var(--bubble-width);
	}
	.bubble {
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		background: var(--bubble);
		padding: 0.6rem 1rem;
		border-radius: var(--radius);
	}
	textarea.bubble {
		box-sizing: border-box;
		width: min(var(--width), 100vw);
		max-width: 100%;
		max-height: 40vh;
		resize: none;
		border: 1px solid var(--line);
		outline: none;
		color: inherit;
		font: inherit;
		line-height: 1.5;
	}
	.command {
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
	.command span {
		color: var(--accent-text);
	}
	.command.failed {
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
	/* the icons under a message, at the size of the chat as those of the sidebar */
	.actions {
		display: flex;
		gap: 0.2rem;
	}
	.actions button {
		display: flex;
		padding: 0.2rem;
		font-size: var(--font-large);
		color: var(--fg-dim);
		background: none;
		border: none;
		cursor: pointer;
	}
	.actions button:hover {
		color: var(--fg);
	}
	.actions button.copied {
		color: var(--ok);
	}
	.actions button:disabled {
		opacity: 0.35;
		cursor: default;
	}
</style>
