<script lang="ts">
	import { app, askOf, send, stop } from '../lib/state.svelte.js';
	import { NEW_DRAFT, draftOf, keep } from '../lib/drafts.js';
	import { known, suggest } from '../engine/run.js';
	import type { Known } from '../engine/engine.js';
	import { extend } from '../engine/complete.js';
	import Icon from './Icon.svelte';
	import { SLASH } from '../lib/config.js';

	// a key typed elsewhere than in a field reaches the composer, unless a modal
	// stands over the page; a touch screen opens no keyboard by itself
	const EDITABLE = 'input, textarea, select, [contenteditable]';
	const MODAL = '[aria-modal="true"]';
	const TOUCH = '(hover: none)';

	let text = $state('');
	let field: HTMLTextAreaElement;

	// each conversation shows its own draft, kept as it is typed
	const key = $derived(app.current?.id ?? NEW_DRAFT);
	$effect.pre(() => {
		text = draftOf(key);
		requestAnimationFrame(resize);
	});

	// the keyboard comes back to the field on another conversation and once
	// the question of the conversation is answered; a question takes it itself
	const asked = $derived(!!askOf(app.current?.id));
	$effect(() => {
		void key;
		if (!asked && !matchMedia(TOUCH).matches) field.focus();
	});

	// the items the modules know, listed again each time a line opens on the CLI
	let items: Known = $state({});
	const cli = $derived(text.startsWith(SLASH));
	$effect(() => {
		if (cli) known('user').then((k) => (items = k));
	});

	// what may follow a slash command, filtered as it is typed
	let hints = $derived(
		cli && !text.includes('\n')
			? suggest(text.slice(SLASH.length), 'user', items, { conversation: app.current ?? undefined })
			: []
	);
	// the slash command completed as far as the candidates agree; a candidate
	// in <> names what to type, it only shows
	function take(found: readonly string[]) {
		const next = SLASH + extend(text.slice(SLASH.length), found);
		if (next === text) return;
		text = next;
		keep(key, text);
		field.focus();
		requestAnimationFrame(resize);
	}

	// the field as tall as its lines, from the first paint on
	function resize() {
		field.style.height = 'auto';
		field.style.height = field.scrollHeight + 'px';
	}
	$effect(resize);

	// whether the model answers in the open conversation now
	const busy = $derived(!!app.current && app.current.id in app.replies);

	// a slash command runs even while the model answers, a message waits
	function submit() {
		const line = text.trim();
		if (!line || (busy && !line.startsWith(SLASH))) return;
		text = '';
		keep(key, text);
		requestAnimationFrame(resize);
		send(line);
	}

	// the turn of the open conversation stops, by the Stop button as by Escape
	function halt() {
		if (busy && app.current) stop(app.current.id);
	}

	// Escape stops the turn, unless something on the page answered it; a
	// character typed outside a field goes to the composer, a shortcut does
	// not: Cmd, or Ctrl without Alt, since AltGr reads as Ctrl+Alt on Windows
	// and Option types characters on macOS
	function onwindowkeydown(e: KeyboardEvent) {
		if (e.defaultPrevented || e.isComposing) return;
		if (e.key === 'Escape') return halt();
		if (e.key.length !== 1 || e.key === ' ' || e.metaKey || (e.ctrlKey && !e.altKey)) return;
		if ((e.target as Element).closest?.(EDITABLE) || document.querySelector(MODAL)) return;
		field.focus();
	}

	// on a CLI line Tab completes, and keeps the keyboard in the field when
	// nothing is left to complete; elsewhere it moves the focus as usual
	function onkeydown(e: KeyboardEvent) {
		if (e.key === 'Tab' && !e.shiftKey && text.startsWith(SLASH)) {
			e.preventDefault();
			if (hints.length) take(hints);
			return;
		}
		if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
			e.preventDefault();
			submit();
		}
	}
</script>

<svelte:window onkeydown={onwindowkeydown} />

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
		oninput={() => (keep(key, text), resize())}
		{onkeydown}
		autocomplete="off"
		placeholder="Message, or {SLASH} for the CLI"
		rows="1"
	></textarea>
	{#if busy}
		<button type="button" onclick={halt} aria-label="Stop"><Icon name="stop" /></button>
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
		font-size: var(--text-secondary);
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
