import type { Outcome } from './types.js';
import type { Scope } from '../engine/engine.js';
import { settings } from '../config.js';
import { rendering } from '../rendering/rendering.js';
import { ERROR_PREFIX } from '../engine/text.js';
import { BACKEND, attach } from './sync.js';

// the words the page runs itself, whatever terminal it talks to, the page or
// its server: attach and detach move the page between its server and its own
// world, rendering runs the rest of the line on the rendering of the page;
// any prefix of one word alone names it
export const WORDS = ['attach', 'detach', 'rendering'] as const;
type Word = (typeof WORDS)[number];

// the word a line opens with, and the rest of the line, none when it opens
// with no word of the page
export function word(line: string): { word: Word; rest: string } | null {
	const head = line.trimStart();
	const first = head.split(/\s/, 1)[0];
	const typed = first.toLowerCase();
	const found = WORDS.filter((w) => typed && w.startsWith(typed));
	return found.length === 1 ? { word: found[0], rest: head.slice(first.length).trimStart() } : null;
}

const refused = (text: string): Outcome => ({ ok: false, text: ERROR_PREFIX + text });

// what a line of a word of the page says once it ran, the questions of the
// rendering asked as the scope asks them
export async function say(line: string, scope: Scope): Promise<Outcome> {
	const said = word(line)!;
	if (said.word === 'rendering') return rendering.run(said.rest, 'user', scope);
	if (said.rest.trim()) return refused(`${said.word} takes nothing after it`);
	if (said.word === 'detach') return (attach(settings, false), { ok: true, text: '' });
	if (!settings.get('mcp url', BACKEND))
		return refused(`no mcp url ${BACKEND}, the KiSS server the page attaches to`);
	attach(settings, true);
	return { ok: true, text: '' };
}

// what may follow a line of a word of the page as it is typed: the words
// themselves while the first word is typed, the rendering after its word
export function hints(line: string, known: Parameters<typeof rendering.suggest>[2]): string[] {
	const head = line.trimStart();
	if (!/\s/.test(head)) return WORDS.filter((w) => w.startsWith(head.toLowerCase()));
	const said = word(line);
	return said?.word === 'rendering' ? rendering.suggest(said.rest, 'user', known) : [];
}
