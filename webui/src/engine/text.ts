// the words of the engine: the constants of the grammar, the notes a line
// prints and the errors it raises

export const COMMENT_PREFIX = '!';
// what stands for a secret kept out of view, in the lines the history keeps;
// never a value, as its brackets tell
export const REMOVED = '<removed>';
export const ERROR_PREFIX = '% ';
export const PIPE = '|';
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
	return new Error(`nothing goes after ${head.join(' ')}: ${rest.join(' ')}`);
}
