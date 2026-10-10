<script lang="ts">
	import { NAME, SLASH } from '../lib/config.js';
	import Message from './Message.svelte';
	import Ask from './Ask.svelte';
	import { app } from '../lib/state.svelte.js';
	import { forks, path } from '../lib/conversation.js';
	import type { Entry } from '../lib/types.js';

	// distance from the bottom under which the thread follows new content
	const FOLLOW_PX = 48;

	let box: HTMLElement;
	let content: HTMLElement;
	let follow = true;

	// the path up from the leaf, and the versions of each entry along it
	const thread = $derived(app.current ? path(app.current) : []);
	const versions = $derived(app.current ? forks(app.current) : new Map<string | null, Entry[]>());
	// the turn the model writes now in the open conversation, if any
	const reply = $derived(app.current ? app.replies[app.current.id] : undefined);

	function onscroll() {
		follow = box.scrollHeight - box.scrollTop - box.clientHeight < FOLLOW_PX;
	}

	$effect(() => {
		const observer = new ResizeObserver(() => {
			if (follow) box.scrollTop = box.scrollHeight;
		});
		observer.observe(content);
		return () => observer.disconnect();
	});
</script>

<div class="thread" bind:this={box} {onscroll}>
	<div class="content" bind:this={content}>
		{#if thread.length}
			{#each thread as entry (entry.id)}
				<Message {entry} versions={versions.get(entry.parent) ?? [entry]} live={entry === reply} />
			{/each}
		{:else}
			<div class="empty">
				<h1>{NAME}</h1>
				<p>Point it at an LLM and just talk. A line starting with {SLASH} runs the CLI.</p>
			</div>
		{/if}
		<Ask />
	</div>
</div>

<style>
	.thread {
		flex: 1;
		overflow-y: auto;
		min-height: 0;
	}
	.content {
		display: flex;
		flex-direction: column;
		gap: 1.2rem;
		max-width: var(--width);
		margin: 0 auto;
		padding: 1.5rem 1rem 2rem;
	}
	.empty {
		text-align: center;
		margin-top: 22vh;
		color: var(--fg-dim);
	}
	h1 {
		font-size: var(--size-title);
		margin: 0 0 0.5rem;
		color: var(--accent-text);
		letter-spacing: -0.03em;
	}
</style>
