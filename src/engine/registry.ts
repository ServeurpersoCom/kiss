import type { Command, Module } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { didYouMean } from './near.js';

const PATH_WORD = /^[a-z][a-z0-9]*$/;

// a file in commands/ or modules/ registers itself by existing
const commandFiles = import.meta.glob<{ default: Command<unknown> }>('../commands/*.ts', {
	eager: true
});
const moduleFiles = import.meta.glob<{ default: Module }>('../modules/*.ts', { eager: true });

export const commands: readonly Command<unknown>[] = Object.values(commandFiles)
	.map((f) => f.default)
	.sort((a, b) => a.path.join(' ').localeCompare(b.path.join(' ')));

export const modules: readonly Module[] = Object.values(moduleFiles).map((f) => f.default);

// a plugin declared wrong or twice stops the page at once, never later
function audit(): string[] {
	const problems: string[] = [];
	const paths = commands.map((c) => c.path.join(' '));
	for (const [i, c] of commands.entries()) {
		const path = paths[i];
		if (!c.path.length || !c.path.every((w) => PATH_WORD.test(w)))
			problems.push(`"${path}": bad path`);
		if (!c.roles.length) problems.push(`${path}: no role`);
		if (paths.indexOf(path) !== i) problems.push(`${path}: declared twice`);
	}
	// a module name after a verb must never read as the next word of a command
	const inner = new Set(commands.flatMap((c) => c.path.slice(1)));
	const names = modules.map((m) => m.name);
	for (const [i, name] of names.entries()) {
		if (!PATH_WORD.test(name)) problems.push(`module "${name}": bad name`);
		if (names.indexOf(name) !== i) problems.push(`module ${name}: declared twice`);
		const clash = [...inner].find((w) => w.startsWith(name) || name.startsWith(w));
		if (clash) problems.push(`module ${name}: reads as the command word ${clash}`);
	}
	return problems;
}

const problems = audit();
if (problems.length) throw new Error(['bad plugins', ...problems].join('\n'));

interface Walk {
	// the deepest command whose whole path the words spell
	command?: Command<unknown>;
	// words consumed by that command's path
	depth: number;
	// commands still matching after the last consumed word
	pool: readonly Command<unknown>[];
	// words consumed by the walk, matched command or not
	consumed: number;
	// the word that matched several path words, with those words
	ambiguous?: { word: string; names: string[] };
}

// IOS lookup: each word may be any unambiguous prefix of a path word, an exact
// word always wins, and the walk stops at the first word no path continues with
export function walk(words: readonly string[], all: readonly Command<unknown>[]): Walk {
	let pool = all;
	let command: Command<unknown> | undefined;
	let depth = 0;
	let i = 0;
	for (; i < words.length; i++) {
		const word = words[i].toLowerCase();
		const next = pool.filter((c) => c.path.length > i && c.path[i].startsWith(word));
		const names = [...new Set(next.map((c) => c.path[i]))];
		if (!names.length) break;
		const name = names.length === 1 ? names[0] : names.find((n) => n === word);
		if (!name) return { command, depth, pool, consumed: i, ambiguous: { word: words[i], names } };
		pool = next.filter((c) => c.path[i] === name);
		const done = pool.find((c) => c.path.length === i + 1);
		if (done) {
			command = done;
			depth = i + 1;
		}
	}
	return { command, depth, pool, consumed: i };
}

export function resolve(words: readonly string[]): { command: Command<unknown>; args: string[] } {
	const w = walk(words, commands);
	if (w.ambiguous) {
		throw new Error(`ambiguous "${w.ambiguous.word}": ${w.ambiguous.names.join(' ')}`);
	}
	if (w.command) return { command: w.command, args: words.slice(w.depth) };
	const names = [...new Set(w.pool.map((c) => c.path[w.consumed]).filter((n) => n))];
	const word = words[w.consumed];
	const head = words.slice(0, w.consumed).join(' ');
	if (word === undefined) throw new Incomplete();
	const what = w.consumed ? `word "${word}" after ${head}` : `command "${word}"`;
	throw new Error(`unknown ${what}${didYouMean(word, names)}`);
}
