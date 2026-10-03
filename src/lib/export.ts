import type { Call, Conversation, Message, Round } from './types.js';
import { NAME } from './config.js';

// a code block whose fence is longer than any run of backticks inside it
function block(text: string, lang = ''): string {
	const longest = Math.max(2, ...(text.match(/`+/g) ?? []).map((run) => run.length));
	const fence = '`'.repeat(longest + 1);
	return `${fence}${lang}\n${text}\n${fence}`;
}

// a part that opens on a click, as the thread shows it
function fold(summary: string, body: string): string {
	return `<details><summary>${summary}</summary>\n\n${body}\n\n</details>`;
}

function call(c: Call): string {
	const parts = [block(c.args, 'json')];
	if (c.result !== undefined) parts.push(block(c.result));
	for (const image of c.images ?? []) parts.push(`![](data:${image.mime};base64,${image.data})`);
	return fold(`${c.name}${c.ok === false ? ' (failed)' : ''}`, parts.join('\n\n'));
}

function round(r: Round): string[] {
	return [
		...(r.reasoning ? [fold('Thinking', r.reasoning)] : []),
		...(r.text ? [r.text] : []),
		...r.calls.map(call)
	];
}

function message(m: Message): string {
	switch (m.role) {
		case 'user':
			return `## User\n\n${m.text}`;
		case 'assistant':
			return [
				'## Assistant',
				...m.rounds.flatMap(round),
				...(m.error ? [`**${m.error}**`] : [])
			].join('\n\n');
		case 'cli':
			return block(`${NAME}# ${m.input}\n${m.output}`);
	}
}

// a conversation as Markdown, read as the thread shows it
export function markdown(c: Conversation): string {
	return [`# ${c.title}`, ...c.messages.map(message)].join('\n\n') + '\n';
}

// the browser saves a conversation as a Markdown file named after its title
export function download(c: Conversation): void {
	const url = URL.createObjectURL(new Blob([markdown(c)], { type: 'text/markdown' }));
	const a = document.createElement('a');
	a.href = url;
	a.download = `${c.title.replace(/[\\/:*?"<>|]/g, '_')}.md`;
	a.click();
	URL.revokeObjectURL(url);
}
