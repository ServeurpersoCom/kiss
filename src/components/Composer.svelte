<script lang="ts">
	import { app, send, stop } from '../lib/state.svelte.js';
	import { suggest } from '../engine/run.js';
	import { extend } from '../engine/complete.js';
	import Icon from './Icon.svelte';
	import { SLASH } from '../lib/config.js';

	let text = $state('');
	let field: HTMLTextAreaElement;

	// what may follow a slash command, filtered as it is typed
	let hints = $derived(
		text.startsWith(SLASH) && !text.includes('\n') ? suggest(text.slice(SLASH.length), 'user') : []
	);
	// the slash command completed as far as the candidates agree; a candidate
	// in <> names what to type, it only shows
	function take(found: readonly string[]) {
		const next = SLASH + extend(text.slice(SLASH.length), found);
		if (next === text) return;
		text = next;
		field.focus();
		requestAnimationFrame(resize);
	}

	// the field as tall as its lines, from the first paint on
	function resize() {
		field.style.height = 'auto';
		field.style.height = field.scrollHeight + 'px';
	}
	$effect(resize);

	// a slash command runs even while the model answers, a message waits
	function submit() {
		const line = text.trim();
		if (!line || (app.reply && !line.startsWith(SLASH))) return;
		text = '';
		requestAnimationFrame(resize);
		send(line);
	}

	function onkeydown(e: KeyboardEvent) {
		if (e.key === 'Tab' && hints.length) {
			e.preventDefault();
			take(hints);
			return;
		}
		if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
			e.preventDefault();
			submit();
		}
	}
</script>

{#if hints.length}
	<div class="hints">
		{#each hints as hint (hint)}
			<button type="button" class:placeholder={hint.startsWith('<')} onclick={() => take([hint])}>
				{hint}
			</button>
		{/each}
	</div>
{/if}

<form
	onsubmit={(e) => {
		e.preventDefault();
		submit();
	}}
>
	<textarea
		bind:this={field}
		bind:value={text}
		class:cli={text.startsWith(SLASH)}
		oninput={resize}
		{onkeydown}
		placeholder="Message, or {SLASH} for the CLI"
		rows="1"
	></textarea>
	{#if app.reply}
		<button type="button" onclick={stop} aria-label="Stop"><Icon name="stop" /></button>
	{:else}
		<button type="submit" disabled={!text.trim()} aria-label="Send"><Icon name="send" /></button>
	{/if}
</form>

<style>
	.hints {
		display: flex;
		flex-wrap: wrap;
		gap: 0.35rem;
		max-width: var(--width);
		max-height: 7.5rem;
		overflow-y: auto;
		margin: 0 auto 0.5rem;
		font-family: var(--mono);
		font-size: var(--size-secondary);
	}
	.hints button {
		width: auto;
		height: auto;
		border-radius: 999px;
		padding: 0.25rem 0.7rem;
		background: var(--surface);
		color: var(--fg);
		border: 1px solid var(--line);
		font: inherit;
	}
	.hints button.placeholder {
		color: var(--fg-dim);
		border-style: dashed;
		cursor: default;
	}
	form {
		display: flex;
		align-items: flex-end;
		gap: 0.5rem;
		max-width: var(--width);
		width: 100%;
		box-sizing: border-box;
		margin: 0 auto;
		padding: 0.6rem 0.6rem 0.6rem 1.1rem;
		background: var(--surface);
		border: 1px solid var(--line);
		border-radius: calc(var(--radius) * 1.6);
		box-shadow: 0 4px 16px var(--shadow);
	}
	/* its height counts its padding, as the scroll height it is given does */
	textarea {
		flex: 1;
		box-sizing: border-box;
		resize: none;
		border: none;
		outline: none;
		background: none;
		color: inherit;
		font: inherit;
		line-height: 1.5;
		max-height: 40vh;
		padding: 0.35rem 0;
	}
	textarea.cli {
		font-family: var(--mono);
	}
	button {
		flex: none;
		display: grid;
		place-items: center;
		width: 2.2rem;
		height: 2.2rem;
		border: none;
		border-radius: 50%;
		background: var(--accent);
		color: var(--on-accent);
		cursor: pointer;
	}
	button:disabled {
		opacity: 0.35;
		cursor: default;
	}
</style>
