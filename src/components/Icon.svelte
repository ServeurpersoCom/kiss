<script lang="ts">
	import { PATHS, stroke, type IconName } from '../lib/icons.js';

	let { name }: { name: IconName } = $props();
</script>

<svg {...stroke(name)}><path d={PATHS[name]} /></svg>

<style>
	svg {
		flex: none;
		display: block;
	}
</style>
