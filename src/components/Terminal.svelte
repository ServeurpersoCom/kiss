<script lang="ts">
	import { run, suggest } from '../engine/run.js';
	import { extend } from '../engine/complete.js';
	import { app } from '../lib/state.svelte.js';
	import { NAME } from '../lib/config.js';

	interface Line {
		text: string;
		kind: 'input' | 'output' | 'error';
	}

	const PROMPT = `${NAME}# `;

	let lines: Line[] = $state([]);
	let input = $state('');
	let past: string[] = [];
	let back = 0;
	let field: HTMLInputElement;
	let box: HTMLElement;

	function print(text: string, kind: Line['kind']) {
		lines.push({ text, kind });
	}

	// the log follows every new line
	$effect(() => {
		void lines.length;
		box.scrollTop = box.scrollHeight;
	});

	// the developer runs exactly like the model: same role, same output
	async function enter() {
		const text = input;
		input = '';
		print(PROMPT + text, 'input');
		if (!text.trim()) return;
		past.push(text);
		back = 0;
		const result = await run(text, 'llm');
		if (result.text) print(result.text, result.ok ? 'output' : 'error');
	}

	// the line goes as far as the candidates agree, else they are listed
	function tab() {
		const found = suggest(input, 'llm');
		const next = extend(input, found);
		if (next !== input) input = next;
		else if (found.length) print(found.join('  '), 'output');
	}

	function onkeydown(e: KeyboardEvent) {
		if (e.key === 'Enter') enter();
		else if (e.key === 'Tab') {
			e.preventDefault();
			tab();
		} else if (e.key === 'ArrowUp' && back < past.length) {
			e.preventDefault();
			input = past[past.length - ++back];
		} else if (e.key === 'ArrowDown' && back > 0) {
			e.preventDefault();
			input = --back ? past[past.length - back] : '';
		} else if (e.key === 'Escape') app.terminal = false;
	}

	$effect(() => field.focus());
</script>

<div class="terminal">
	<div class="log" bind:this={box}>
		{#each lines as line, i (i)}
			<pre class={line.kind}>{line.text}</pre>
		{/each}
	</div>
	<label>
		<span>{PROMPT}</span>
		<input bind:this={field} bind:value={input} {onkeydown} spellcheck="false" autocomplete="off" />
	</label>
</div>

<style>
	.terminal {
		position: fixed;
		left: 0.75rem;
		right: 0.75rem;
		bottom: 0.75rem;
		height: 45vh;
		display: flex;
		flex-direction: column;
		background: #15141a;
		color: #fbfbfe;
		border-radius: var(--radius);
		box-shadow: 0 12px 40px var(--shadow);
		font-family: var(--mono);
		font-size: var(--font-small);
		padding: 0.75rem 1rem;
		box-sizing: border-box;
		z-index: 10;
	}
	.log {
		flex: 1;
		overflow-y: auto;
	}
	pre {
		margin: 0;
		white-space: pre-wrap;
		overflow-wrap: anywhere;
		font-family: inherit;
	}
	.input {
		color: #a8a8b3;
	}
	.error {
		color: #ff848b;
	}
	label {
		display: flex;
		white-space: pre;
	}
	input {
		flex: 1;
		background: none;
		border: none;
		outline: none;
		color: inherit;
		font: inherit;
		padding: 0;
	}
</style>
