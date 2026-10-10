<script lang="ts">
	import type { Attachment } from 'svelte/attachments';
	import type { Verdict } from '../lib/types.js';
	import { always, answers } from '../lib/types.js';
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
	// each question starts with an empty field, whatever settled the one before
	$effect.pre(() => {
		void app.asks[0]?.id;
		value = '';
	});

	// the first way to answer takes the keyboard as the question shows
	const focused: Attachment<HTMLElement> = (node) => node.focus();

	// a secret given by OK or Enter once typed, none by Cancel or Escape
	function give(typed: string | null) {
		const ask = app.asks[0];
		if (ask?.kind !== 'secret' || typed === '') return;
		ask.settle(typed);
	}

	// the title of the conversation a question comes from
	const title = (id: string): string => app.conversations.find((c) => c.id === id)?.title ?? '';

	function onkeydown(e: KeyboardEvent) {
		if (e.key === 'Escape') give(null);
		else if (e.key === 'Enter') give(value);
	}
</script>

<!-- the first question waiting, each one drawn anew; one from another
     conversation names it, as it holds the CLI until answered -->
{#each app.asks.slice(0, 1) as ask (ask.id)}
	<div class="ask">
		{#if ask.from !== app.current?.id}
			<div class="note">From {title(ask.from)}</div>
		{/if}
		{#if ask.kind === 'grant'}
			{@const grants = always(ask.request)}
			{#if ask.request.kind === 'call'}
				<div class="head">Allow this call of {ask.request.tool}?</div>
				<pre>{ask.request.args}</pre>
			{:else}
				<div class="head">Allow this change?</div>
				<pre>{ask.request.lines.join('\n')}</pre>
			{/if}
			{#if grants}
				<div class="note">Always {grants}</div>
			{/if}
			<div class="answers">
				{#each answers(ask.request) as verdict, i (verdict)}
					<button {@attach i === 0 && focused} onclick={() => ask.settle(verdict)}>
						{LABELS[verdict]}
					</button>
				{/each}
			</div>
		{:else if ask.kind === 'offer'}
			<div class="head">Save {ask.name}, {size(ask.text)}?</div>
			<div class="answers">
				<button {@attach focused} onclick={() => (deliver(ask.name, ask.text), ask.settle(true))}>
					Save
				</button>
				<button onclick={() => ask.settle(false)}>Cancel</button>
			</div>
		{:else if ask.kind === 'pick'}
			<div class="head">Import conversations from a file?</div>
			<div class="answers">
				<button {@attach focused} onclick={() => open(ask.settle)}>Choose file</button>
				<button onclick={() => ask.settle(null)}>Cancel</button>
			</div>
		{:else if ask.kind === 'confirm'}
			<div class="head">{ask.question}</div>
			<div class="answers">
				<button {@attach focused} onclick={() => ask.settle(true)}>OK</button>
				<button onclick={() => ask.settle(false)}>Cancel</button>
			</div>
		{:else}
			<div class="head">Value of {ask.key}</div>
			<input
				{@attach focused}
				bind:value
				{onkeydown}
				type="password"
				autocomplete="off"
				spellcheck="false"
			/>
			<div class="answers">
				<button onclick={() => give(value)} disabled={!value}>OK</button>
				<button onclick={() => give(null)}>Cancel</button>
			</div>
		{/if}
	</div>
{/each}

<style>
	/* a question to the user, marked by the accent */
	.ask {
		display: flex;
		flex-direction: column;
		gap: 0.4rem;
		padding: 0.4rem 0 0.4rem 0.9rem;
		border-left: 2px solid var(--accent);
		font-size: var(--size-secondary);
	}
	.head {
		color: var(--accent-text);
	}
	.note {
		color: var(--fg-dim);
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
