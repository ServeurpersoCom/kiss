<script lang="ts">
	import type { Attachment } from 'svelte/attachments';
	import type { Verdict } from '../lib/types.js';
	import { VERDICTS } from '../lib/types.js';
	import { app, choose, deliver } from '../lib/state.svelte.js';

	const LABELS: Record<Verdict, string> = { once: 'Once', always: 'Always', refuse: 'Refuse' };
	const KIB = 1024;

	// the size of a file as it reads, in kilobytes or megabytes
	function size(text: string): string {
		const kib = new Blob([text]).size / KIB;
		return kib < KIB ? `${kib.toFixed(1)} KB` : `${(kib / KIB).toFixed(1)} MB`;
	}

	// the picker opens on the click itself; a picker closed on nothing keeps the
	// question, which Cancel answers
	function open(settle: (text: string | null) => void) {
		void choose().then((text) => text !== null && settle(text));
	}

	let value = $state('');

	// the first way to answer takes the keyboard as the question shows
	const focused: Attachment<HTMLElement> = (node) => node.focus();

	function onkeydown(e: KeyboardEvent) {
		if (app.ask?.kind !== 'secret') return;
		if (e.key === 'Escape') app.ask.settle(null);
		else if (e.key === 'Enter' && value) {
			const typed = value;
			value = '';
			app.ask.settle(typed);
		}
	}
</script>

{#if app.ask}
	<div class="ask">
		{#if app.ask.kind === 'grant'}
			{@const ask = app.ask}
			{#if ask.request.kind === 'call'}
				<div class="head">Allow this call of {ask.request.tool}?</div>
				<pre>{ask.request.args}</pre>
			{:else}
				<div class="head">Allow this change?</div>
				<pre>{ask.request.lines.join('\n')}</pre>
			{/if}
			<div class="answers">
				{#each VERDICTS as verdict, i (verdict)}
					<button {@attach i === 0 && focused} onclick={() => ask.settle(verdict)}>
						{LABELS[verdict]}
					</button>
				{/each}
			</div>
		{:else if app.ask.kind === 'offer'}
			{@const ask = app.ask}
			<div class="head">Save {ask.name}, {size(ask.text)}?</div>
			<div class="answers">
				<button {@attach focused} onclick={() => (deliver(ask.name, ask.text), ask.settle(true))}>
					Save
				</button>
				<button onclick={() => ask.settle(false)}>Cancel</button>
			</div>
		{:else if app.ask.kind === 'pick'}
			{@const ask = app.ask}
			<div class="head">Import conversations from a file?</div>
			<div class="answers">
				<button {@attach focused} onclick={() => open(ask.settle)}>Choose file</button>
				<button onclick={() => ask.settle(null)}>Cancel</button>
			</div>
		{:else}
			<div class="head">Value of {app.ask.key}</div>
			<input
				{@attach focused}
				bind:value
				{onkeydown}
				type="password"
				autocomplete="off"
				spellcheck="false"
			/>
		{/if}
	</div>
{/if}

<style>
	/* a question to the user: read like the CLI, marked by the accent */
	.ask {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
		padding: 0.4rem 0 0.4rem 0.9rem;
		border-left: 2px solid var(--accent);
		font-family: var(--mono);
		font-size: var(--font-small);
	}
	.head {
		color: var(--accent-text);
	}
	pre {
		margin: 0;
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		font: inherit;
	}
	.answers {
		display: flex;
		gap: 0.35rem;
	}
	button,
	input {
		font: inherit;
		color: var(--fg);
		background: var(--surface);
		border: 1px solid var(--line);
		outline: none;
	}
	button {
		padding: 0.25rem 0.7rem;
		border-radius: 999px;
		cursor: pointer;
	}
	input {
		padding: 0.35rem 0.6rem;
		border-radius: calc(var(--radius) * 0.6);
	}
	button:hover,
	button:focus-visible,
	input:focus {
		border-color: var(--accent);
	}
</style>
