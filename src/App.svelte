<script lang="ts">
	import Sidebar from './components/Sidebar.svelte';
	import Thread from './components/Thread.svelte';
	import Composer from './components/Composer.svelte';
	import Terminal from './components/Terminal.svelte';
	import Splitter from './components/Splitter.svelte';
	import Icon from './components/Icon.svelte';
	import { app } from './lib/state.svelte.js';
	import { NAME } from './lib/config.js';

	// the width of the sidebar the user dragged, 0 when closed, none before a
	// drag
	let width: number | null = $state(null);

	// the developer terminal has no visible way in, only this key
	function onkeydown(e: KeyboardEvent) {
		if (e.ctrlKey && e.code === 'Backquote') {
			e.preventDefault();
			app.terminal = !app.terminal;
		}
	}
</script>

<svelte:window {onkeydown} />

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
			<span>{app.current?.title ?? NAME}</span>
		</header>
		<Thread />
		<div class="composer"><Composer /></div>
	</section>
</main>

{#if app.terminal}
	<Terminal />
{/if}

<style>
	/* the tokens of the page: plain neutral grays around the accent, each color
	   given for light then dark, then the shape, the fonts and the widths */
	:global(:root) {
		color-scheme: light dark;
		--accent: oklch(0.6 0.16 250);
		--accent-text: light-dark(var(--accent), color-mix(in oklab, var(--accent) 75%, white));
		/* black or white, whichever reads on the accent */
		--on-accent: oklch(from var(--accent) clamp(0, (0.72 - l) * 1000, 1) 0 0);
		--base: light-dark(#faf9f5, #262624);
		--sidebar: light-dark(#f5f4ed, #1f1e1d);
		--surface: light-dark(#ffffff, #30302e);
		--bubble: light-dark(#f0eee6, #141413);
		--fg: light-dark(#141413, #faf9f5);
		--fg-dim: light-dark(#73726c, #9c9a92);
		--line: light-dark(rgb(31 30 29 / 0.15), rgb(250 249 245 / 0.12));
		--hover: light-dark(rgb(31 30 29 / 0.05), rgb(250 249 245 / 0.05));
		--hover-strong: light-dark(rgb(31 30 29 / 0.09), rgb(250 249 245 / 0.09));
		--shadow: light-dark(rgb(31 30 29 / 0.06), rgb(0 0 0 / 0.25));
		--code-bg: light-dark(rgb(31 30 29 / 0.05), rgb(250 249 245 / 0.06));
		--code-string: light-dark(#4d7c0f, #a3e635);
		--code-number: light-dark(#b45309, #fdba74);
		--code-title: light-dark(#1d4ed8, #93c5fd);
		--ok: light-dark(#4d7c0f, #a3e635);
		--danger: light-dark(#b91c1c, #f87171);
		--radius: 12px;
		--font: system-ui, sans-serif;
		--mono: ui-monospace, monospace;
		/* the two sizes of every text of the page: the chat, then everything
		   around it, thinking, tools, code, the CLI; the name of the page alone
		   has a size of its own */
		--font-large: 1rem;
		--font-small: 0.8rem;
		--font-name: 1.5rem;
		/* the thread and the composer, the bubble of the user, the sidebar */
		--width: 48rem;
		--bubble-width: 85%;
		--sidebar-width: 16rem;
		--sidebar-min: 12rem;
		--sidebar-max: 24rem;
	}
	:global(html, body, #app) {
		height: 100%;
		margin: 0;
	}
	:global(body) {
		font-family: var(--font);
		font-size: var(--font-large);
		color: var(--fg);
		background: var(--base);
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
		font-size: var(--font-large);
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
