<script lang="ts">
	// the icons of the page, drawn on a grid of 24, stroked at 2 with round
	// ends, in the color and at the size of the text around them; stop alone is
	// filled
	const PATHS = {
		send: 'M12 19V5M5 12l7-7 7 7',
		stop: 'M8 8h8v8H8z',
		download: 'M12 4v11M7 10l5 5 5-5M5 20h14',
		close: 'M6 6l12 12M18 6L6 18',
		menu: 'M4 6h16M4 12h16M4 18h16',
		chevron: 'M9 6l6 6-6 6'
	};

	let { name }: { name: keyof typeof PATHS } = $props();
</script>

<svg
	viewBox="0 0 24 24"
	width="1em"
	height="1em"
	fill={name === 'stop' ? 'currentColor' : 'none'}
	stroke="currentColor"
	stroke-width="2"
	stroke-linecap="round"
	stroke-linejoin="round"
	aria-hidden="true"><path d={PATHS[name]} /></svg
>

<style>
	svg {
		flex: none;
		display: block;
	}
</style>
