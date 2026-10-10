<script lang="ts">
	import type { Entry } from '../lib/types.js';
	import Round from './Round.svelte';
	import Status from './Status.svelte';
	import Icon from './Icon.svelte';
	import { CLI, COPIED_MS, SLASH, ago, stamp } from '../lib/config.js';
	import { clock } from '../lib/clock.svelte.js';
	import { source } from '../lib/conversation.js';
	import { app, browse, dismiss, edit, pulseOf, retry } from '../lib/state.svelte.js';
	import { summary } from '../lib/pulse.js';

	// versions: the entries beside this one, itself among them, oldest first;
	// live: the turn the model writes now; last: the message the thread ends on
	let {
		entry: message,
		versions,
		live = false,
		last = false
	}: { entry: Entry; versions: Entry[]; live?: boolean; last?: boolean } = $props();

	const at = $derived(versions.findIndex((v) => v.id === message.id));
	// whether the model answers in the open conversation now
	const busy = $derived(!!app.current && app.current.id in app.replies);
	// the clock of the turn written now, hidden while a question of it stands,
	// as the time the user takes is none of the system
	const pulse = $derived(live && app.current ? pulseOf(app.current.id) : undefined);
	const asking = $derived(app.asks.some((q) => q.from === app.current?.id));

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

	// the edit goes as a new version, none while it is empty or a turn runs
	function save() {
		const text = draft?.trim();
		if (!text || busy) return;
		draft = null;
		void edit(message.id, text);
	}

	function cancel() {
		draft = null;
	}

	function onkeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') cancel();
		else if (e.key === 'Enter' && !e.shiftKey && !e.isComposing) {
			e.preventDefault();
			save();
		}
	}
</script>

{#snippet clip()}
	<button onclick={copy} class:copied aria-label="Copy">
		<Icon name={copied ? 'check' : 'copy'} />
	</button>
{/snippet}

{#snippet again()}
	<button onclick={() => retry(message.id)} disabled={busy} aria-label="Retry">
		<Icon name="retry" />
	</button>
{/snippet}

{#snippet when()}
	<span class="time" title={stamp(message.time)}>{ago(message.time, clock.now)}</span>
{/snippet}

{#snippet switcher()}
	{#if versions.length > 1}
		<button
			onclick={() => browse(versions[at - 1].id)}
			disabled={at === 0 || busy}
			aria-label="Previous version"
		>
			<Icon name="previous" />
		</button>
		<span class="version">{at + 1}/{versions.length}</span>
		<button
			onclick={() => browse(versions[at + 1].id)}
			disabled={at === versions.length - 1 || busy}
			aria-label="Next version"
		>
			<Icon name="chevron" />
		</button>
	{/if}
{/snippet}

<div class="message {message.role}" class:last class:editing={draft !== null}>
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
		<!-- a command reads like a block of code: a head naming the CLI, closed
		     from its corner, then the line typed and its output -->
		<div class="command">
			<div class="head">
				{CLI}
				<button onclick={() => dismiss(message.id)} disabled={busy} aria-label="Close">
					<Icon name="close" />
				</button>
			</div>
			<pre class:failed={!message.ok}><span>{SLASH}{message.input}</span>{#if message.output}<br
					/>{message.output}{/if}</pre>
		</div>
	{:else}
		<div class="turn">
			{#each message.rounds as round, i (i)}
				<Round {round} live={live && i === message.rounds.length - 1} />
			{/each}
			{#if message.error}
				<div class="error">{message.error}</div>
			{/if}
			{#if pulse && !asking}
				<Status {pulse} />
			{/if}
		</div>
	{/if}
	{#if !live}
		<!-- copy holds the outer edge: last under what the user writes, on the
		     right, first under an answer; the versions and the time sit on the
		     inner side; an edit takes the row for its own two, cancel on the
		     outer edge -->
		<div class="actions">
			{#if draft !== null}
				<button onclick={save} disabled={!draft.trim() || busy} aria-label="Save">
					<Icon name="check" />
				</button>
				<button onclick={cancel} aria-label="Cancel">
					<Icon name="close" />
				</button>
			{:else if message.role === 'user'}
				{@render when()}
				{@render switcher()}
				{@render again()}
				<button onclick={begin} disabled={busy} aria-label="Edit">
					<Icon name="edit" />
				</button>
				{@render clip()}
			{:else if message.role === 'assistant'}
				{@render clip()}
				{@render again()}
				{@render switcher()}
				{@render when()}
				{#if message.stats}
					<span class="time">{summary(message.stats)}</span>
				{/if}
			{:else}
				{@render when()}
				{@render clip()}
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
	/* what the user writes and the commands they type stand on the right, on
	   the background of the user */
	.user,
	.cli {
		align-self: flex-end;
		align-items: flex-end;
		max-width: var(--bubble-width);
	}
	.bubble {
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		background: var(--user-bg);
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
		background: var(--user-bg);
		border-radius: calc(var(--radius) * 0.6);
		font-size: var(--text-secondary);
		color: var(--fg-dim);
	}
	.command .head {
		display: flex;
		justify-content: space-between;
		align-items: center;
		padding: 0.4rem 0.6rem 0 1rem;
	}
	/* the command and its output in monospace, as a terminal shows them */
	.command pre {
		margin: 0;
		padding: 0.5rem 1rem 0.8rem;
		font: inherit;
		font-family: var(--mono);
		white-space: pre-wrap;
		overflow-wrap: anywhere;
	}
	.command span {
		color: var(--accent-text);
	}
	.command pre.failed {
		color: var(--danger);
	}
	.turn {
		display: flex;
		flex-direction: column;
		gap: 0.6rem;
	}
	/* what stopped the turn, read like a question to the user but marked by
	   the danger color */
	.error {
		padding: 0.4rem 0 0.4rem 0.9rem;
		border-left: 2px solid var(--danger);
		font-size: var(--text-secondary);
		color: var(--danger);
		white-space: pre-wrap;
		overflow-wrap: anywhere;
	}
	.error::first-letter {
		text-transform: uppercase;
	}
	/* the icons under a message, shown while the message is hovered or holds
	   the focus, their place kept; always shown under the last message, under
	   one being edited, and where nothing hovers */
	.actions {
		display: flex;
		align-items: center;
		gap: 0.2rem;
		visibility: hidden;
	}
	.message:hover .actions,
	.message:focus-within .actions,
	.message.last .actions,
	.message.editing .actions {
		visibility: visible;
	}
	@media (hover: none) {
		.actions {
			visibility: visible;
		}
	}
	.time {
		padding: 0 0.4rem;
		font-size: var(--text-secondary);
		color: var(--fg-dim);
	}
	/* the time and what the turn spent, their first letter capital */
	.time::first-letter {
		text-transform: uppercase;
	}
	.version {
		font-size: var(--text-secondary);
		color: var(--fg-dim);
		font-variant-numeric: tabular-nums;
	}
	.actions button,
	.command button {
		display: flex;
		padding: 0.2rem;
		color: var(--fg-dim);
		background: none;
		border: none;
		cursor: pointer;
	}
	.actions button:hover,
	.command button:hover {
		color: var(--fg);
	}
	.actions button.copied {
		color: var(--ok);
	}
	.actions button:disabled,
	.command button:disabled {
		opacity: 0.35;
		cursor: default;
	}
</style>
