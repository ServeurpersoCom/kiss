<script lang="ts">
	import Sidebar from './components/Sidebar.svelte';
	import Thread from './components/Thread.svelte';
	import Composer from './components/Composer.svelte';
	import Splitter from './components/Splitter.svelte';
	import Icon from './components/Icon.svelte';
	import { app } from './lib/state.svelte.js';
	import { NAME } from './lib/config.js';

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
			<span>{app.current?.title ?? NAME}</span>
		</header>
		<Thread />
		<div class="composer"><Composer /></div>
	</section>
</main>

<style>
	/* the tokens of the page: three colors, every other color derived from
	   them, then the shape, the fonts, the sizes and the widths; a sheet sets
	   the three for a whole theme, or any derived one alone */
	:global(:root) {
		--bg: oklch(0.17 0.005 260);
		--fg: #f2f1ef;
		--accent: oklch(0.68 0.18 250);
		/* shades of the background: darker for the sidebar and what the user
		   writes, lighter for what holds a field */
		--sidebar: oklch(from var(--bg) calc(l - 0.032) c h);
		--user-bg: oklch(from var(--bg) calc(l - 0.077) c h);
		--surface: oklch(from var(--bg) calc(l + 0.04) c h);
		/* shares of the text over the background */
		--fg-dim: color-mix(in oklab, var(--fg) 58%, var(--bg));
		--line: color-mix(in oklab, var(--fg) 13%, transparent);
		--hover: color-mix(in oklab, var(--fg) 5%, transparent);
		--hover-strong: color-mix(in oklab, var(--fg) 9%, transparent);
		--code-bg: color-mix(in oklab, var(--fg) 6%, transparent);
		/* black, the lighter the background the fainter, and the half black that
		   dims the page behind a dialog */
		--shadow: oklch(from var(--bg) 0 0 0 / calc(0.3 - l * 0.25));
		--veil: oklch(0 0 0 / 0.5);
		/* the accent drawn toward the text, and black or white, whichever reads
		   on the accent */
		--accent-text: color-mix(in oklab, var(--accent) 75%, var(--fg));
		--on-accent: oklch(from var(--accent) clamp(0, (0.6 - l) * 1000, 1) 0 0);
		/* colors with a meaning: a hue at one lightness that follows the text, so
		   it reads on a dark background as on a light one, with the most chroma
		   the screen shows at both without bending the hue */
		--ok: oklch(from var(--fg) calc(l * 0.37 + 0.42) 0.3 145);
		--danger: oklch(from var(--fg) calc(l * 0.37 + 0.42) 0.3 25);
		--code-string: var(--ok);
		--code-number: oklch(from var(--fg) calc(l * 0.37 + 0.42) 0.13 60);
		--code-title: oklch(from var(--fg) calc(l * 0.37 + 0.42) 0.15 255);
		--radius: 12px;
		--font: system-ui, sans-serif;
		--mono: ui-monospace, monospace;
		/* the two sizes of every text of the page: primary for the chat,
		   secondary for everything around it, thinking, tools, code, the CLI;
		   the title of the page has a size of its own, and every icon the
		   primary one */
		--size-primary: 1rem;
		--size-secondary: 0.8rem;
		--size-title: 1.5rem;
		--size-icon: var(--size-primary);
		/* the thread and the composer, the bubble of the user, the sidebar */
		--width: 48rem;
		--bubble-width: 85%;
		--sidebar-width: 16rem;
		--sidebar-min: 12rem;
		--sidebar-max: 24rem;
	}
	:global(.icon) {
		width: var(--size-icon);
		height: var(--size-icon);
	}
	:global(html, body, #app) {
		height: 100%;
		margin: 0;
	}
	:global(body) {
		font-family: var(--font);
		font-size: var(--size-primary);
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
		font-size: var(--size-primary);
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
