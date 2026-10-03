<script lang="ts">
	import { SIDEBAR_STORAGE_KEY } from '../lib/config.js';

	// the edge of the sidebar: the sidebar follows the pointer for the whole
	// drag, within the bounds of the page style; it closes once the pointer
	// goes below half the least width, and opens again once the pointer
	// reaches that width, so the edge never jumps under the pointer; the
	// browser keeps the width a drag leaves, 0 for closed
	let { width = $bindable() }: { width: number | null } = $props();

	let zone: HTMLDivElement;
	let dragging = $state(false);

	try {
		const kept = localStorage.getItem(SIDEBAR_STORAGE_KEY);
		width = kept === null ? null : Number(kept);
	} catch {
		width = null;
	}

	// the left of this edge in the page: the width the sidebar shows
	function left(x: number): number {
		return x - zone.parentElement!.getBoundingClientRect().left;
	}

	function onpointerdown(e: PointerEvent) {
		zone.setPointerCapture(e.pointerId);
		dragging = true;
	}

	function onpointermove(e: PointerEvent) {
		if (!dragging) return;
		const x = left(e.clientX);
		const least = parseFloat(getComputedStyle(zone).maxWidth);
		if (width !== 0 && x < least / 2) width = 0;
		else if (width !== 0 || x >= least) width = x;
	}

	function onpointerup() {
		if (!dragging) return;
		dragging = false;
		if (width !== 0) width = left(zone.getBoundingClientRect().left);
		try {
			localStorage.setItem(SIDEBAR_STORAGE_KEY, String(width));
		} catch {
			// the browser keeps nothing: the sidebar lasts as long as the page
		}
	}
</script>

<div
	bind:this={zone}
	class="edge"
	class:closed={width === 0}
	class:dragging
	role="separator"
	aria-orientation="vertical"
	aria-label="Resize the conversations"
	{onpointerdown}
	{onpointermove}
	{onpointerup}
	onpointercancel={onpointerup}
>
	<span></span>
</div>

<style>
	/* a column of no width, its grip area overflowing on both sides; its max
	   width, of no effect on it, gives the script the least width in pixels */
	.edge {
		position: relative;
		z-index: 2;
		max-width: var(--sidebar-min);
		cursor: col-resize;
		touch-action: none;
	}
	.edge::before {
		content: '';
		position: absolute;
		inset: 0 -4px;
	}
	/* the grip, shown while the pointer is over the sidebar or its edge */
	span {
		display: none;
		position: absolute;
		top: 50%;
		left: -2px;
		width: 4px;
		height: 2.5rem;
		translate: 0 -50%;
		border-radius: 2px;
		background: var(--accent);
	}
	:global(aside:hover) + .edge span,
	.edge:hover span,
	.dragging span {
		display: block;
	}
	/* closed: the area covers the left of the page, the grip shows near it */
	.closed::before {
		inset: 0 auto 0 0;
		width: 1rem;
	}
	.closed span {
		left: 4px;
	}
</style>
