<script lang="ts">
	// a question the user confirms or cancels over the page: Enter or OK
	// confirms, Escape, Cancel or a click outside the card cancels
	let {
		open = $bindable(false),
		title,
		onconfirm
	}: { open: boolean; title: string; onconfirm: () => void } = $props();

	let card: HTMLDivElement | undefined = $state();

	function cancel() {
		open = false;
	}

	// the answer first, while what the question is about still holds
	function confirm() {
		onconfirm();
		open = false;
	}

	// listening only while open; a key it answers goes no further, so Enter
	// never clicks the button that opened the question
	$effect(() => {
		if (!open) return;
		const onmousedown = (e: MouseEvent) => {
			if (!card?.contains(e.target as Node)) cancel();
		};
		const onkeydown = (e: KeyboardEvent) => {
			if (e.key !== 'Escape' && e.key !== 'Enter') return;
			e.preventDefault();
			if (e.key === 'Escape') cancel();
			else confirm();
		};
		document.addEventListener('mousedown', onmousedown);
		document.addEventListener('keydown', onkeydown);
		return () => {
			document.removeEventListener('mousedown', onmousedown);
			document.removeEventListener('keydown', onkeydown);
		};
	});
</script>

{#if open}
	<div class="veil">
		<div class="card" role="dialog" aria-modal="true" aria-label={title} bind:this={card}>
			<div class="title">{title}</div>
			<div class="buttons">
				<button onclick={cancel}>Cancel</button>
				<button onclick={confirm}>OK</button>
			</div>
		</div>
	</div>
{/if}

<style>
	.veil {
		position: fixed;
		inset: 0;
		display: flex;
		align-items: center;
		justify-content: center;
		background: var(--veil);
		z-index: 20;
	}
	.card {
		display: flex;
		flex-direction: column;
		gap: 0.75rem;
		min-width: 16rem;
		padding: 1rem;
		background: var(--surface);
		border: 1px solid var(--line);
		border-radius: var(--radius);
		box-shadow: 0 12px 40px var(--shadow);
	}
	.buttons {
		display: flex;
		justify-content: flex-end;
		gap: 0.35rem;
	}
	button {
		font: inherit;
		font-size: var(--size-secondary);
		color: var(--fg);
		background: var(--bg);
		border: 1px solid var(--line);
		border-radius: 999px;
		padding: 0.25rem 0.9rem;
		cursor: pointer;
	}
	button:hover {
		border-color: var(--accent);
	}
</style>
