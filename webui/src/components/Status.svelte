<script lang="ts">
	import type { Attachment } from 'svelte/attachments';
	import { line, type Pulse } from '../lib/pulse.js';

	// the clock of the turn the model writes now
	let { pulse }: { pulse: Pulse } = $props();

	// the line written straight into its node at every frame of the screen,
	// from its first paint on, the state of the page never touched
	const tick: Attachment<HTMLElement> = (node) => {
		node.textContent = line(pulse, performance.now());
		let frame = requestAnimationFrame(function draw(now) {
			node.textContent = line(pulse, now);
			frame = requestAnimationFrame(draw);
		});
		return () => cancelAnimationFrame(frame);
	};
</script>

<div class="status" {@attach tick}></div>

<style>
	/* what the system does now, in figures that keep their width */
	.status {
		font-size: var(--text-secondary);
		color: var(--fg-dim);
		font-variant-numeric: tabular-nums;
	}
</style>
