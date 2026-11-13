import { SPACE } from '../lib/config.js';

// the words of the engine: the constants of the grammar, the notes a line
// prints and the errors it raises

// what opens a line of a change: what goes, then what comes
export const GONE_PREFIX = '- ';
export const COME_PREFIX = '+ ';
// a word that names what to type where a completion lists it, <name>, and
// whether a word is one
const HINT_OPEN = '<';
export const hint = (what: string): string => `${HINT_OPEN}${what}>`;
export const hinted = (word: string): boolean => word.startsWith(HINT_OPEN);
export const COMMENT_PREFIX = '!';
// what stands for a secret kept out of view, in the lines the history keeps;
// never a value, as its brackets tell
export const REMOVED = '<removed>';
export const ERROR_PREFIX = '% ';
export const PIPE = '|';
// the two commands a configuration is written in
export const SET = 'set';
export const NO = 'no';
// what a filter that keeps no line says, and what a batch that fails says last
export const NO_MATCH = 'no line matches';
export const NOTHING_APPLIED = 'nothing applied';
// the configuration the page runs, and the one it starts with, as IOS names
// them; neither is a save name
export const RUNNING_CONFIG = 'running-config';
export const STARTUP_CONFIG = 'startup-config';
// the word show running-config takes to list every key
export const ALL = 'all';

// a command that misses words: the line lists what may follow instead of failing
export class Incomplete extends Error {}

// a line of output that is not a command: a heading, a note, a warning
export const comment = (text: string): string => `${COMMENT_PREFIX} ${text}`;

// the words a line holds past its end: what they follow, then themselves
export function beyond(head: readonly string[], rest: readonly string[]): Error {
	return new Error(`nothing goes after ${head.join(SPACE)}: ${rest.join(SPACE)}`);
}
