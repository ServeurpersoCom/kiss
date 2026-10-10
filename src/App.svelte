<script lang="ts">
	import Sidebar from './components/Sidebar.svelte';
	import Thread from './components/Thread.svelte';
	import Composer from './components/Composer.svelte';
	import Splitter from './components/Splitter.svelte';
	import Icon from './components/Icon.svelte';
	import { app } from './lib/state.svelte.js';
	import { CLI, NAME } from './lib/config.js';

	// the width of the sidebar the user dragged, 0 when closed, none before a
	// drag
	let width: number | null = $state(null);
</script>

<main
	class:drawer={app.sidebar}
	class:closed={width === 0}
	style:--sidebar-drag={width === null ? null : `${width}px`}
>
	<Sidebar />
	<Splitter bind:width />
	<button class="shade" onclick={() => (app.sidebar = false)} aria-label="Close the conversations"
	></button>
	<section>
		<header>
			<button onclick={() => (app.sidebar = true)} aria-label="Conversations"
				><Icon name="menu" /></button
			>
			<span>{app.current ? app.current.title || CLI : NAME}</span>
		</header>
		<Thread />
		<div class="composer"><Composer /></div>
	</section>
</main>

<style>
	:global(.icon) {
		width: var(--icon);
		height: var(--icon);
	}
	:global(html, body, #app) {
		height: 100%;
		margin: 0;
	}
	:global(body) {
		font-family: var(--font);
		font-size: var(--text-primary);
		color: var(--fg);
		background: var(--bg);
	}
	/* every scrollbar thin, its thumb a line */
	:global(*) {
		scrollbar-width: thin;
		scrollbar-color: var(--line) transparent;
	}
	:global(::placeholder) {
		color: var(--fg-dim);
	}
	/* the sidebar, its edge, the thread; the width a drag sets stays within the
	   bounds of the style */
	main {
		display: grid;
		grid-template-columns:
			clamp(var(--sidebar-min), var(--sidebar-drag, var(--sidebar-width)), var(--sidebar-max))
			0 1fr;
		height: 100%;
	}
	main.closed {
		grid-template-columns: 0 1fr;
	}
	main.closed > :global(aside) {
		display: none;
	}
	section {
		display: flex;
		flex-direction: column;
		min-height: 0;
		min-width: 0;
	}
	.composer {
		padding: 0 1rem 1rem;
	}
	header,
	.shade {
		display: none;
	}
	header button {
		font: inherit;
		font-size: var(--text-primary);
		color: inherit;
		background: none;
		border: none;
		cursor: pointer;
		padding: 0.25rem 0.5rem;
	}
	header span {
		overflow: hidden;
		white-space: nowrap;
		text-overflow: ellipsis;
		font-weight: 600;
	}
	/* a narrow screen keeps the thread and slides the conversations over it */
	@media (max-width: 720px) {
		main,
		main.closed {
			grid-template-columns: 1fr;
		}
		main.closed > :global(aside) {
			display: flex;
		}
		main > :global(.edge) {
			display: none;
		}
		header {
			display: flex;
			align-items: center;
			gap: 0.25rem;
			padding: 0.5rem;
			border-bottom: 1px solid var(--line);
		}
		main > :global(aside) {
			position: fixed;
			inset: 0 auto 0 0;
			width: min(18rem, 85vw);
			z-index: 5;
			transform: translateX(-100%);
			transition: transform 0.2s ease;
		}
		main.drawer > :global(aside) {
			transform: none;
		}
		main.drawer .shade {
			display: block;
			position: fixed;
			inset: 0;
			z-index: 4;
			border: none;
			background: rgb(0 0 0 / 0.35);
		}
		.composer {
			padding: 0 0.5rem 0.5rem;
		}
	}
</style>
