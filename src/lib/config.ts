// the name the page shows
export const NAME = 'KiSS';
// the scoping class of a component is its name, svelte-composer for
// Composer.svelte, so show css tells whose a rule is and a style sheet aims at
// one component by the selector it reads there
export const scope = ({ name }: { name: string }): string => `svelte-${name.toLowerCase()}`;

// engine
export const ARCHIVE_STORAGE_KEY = 'kiss.saves';
export const STARTUP_STORAGE_KEY = 'kiss.startup';
// a name of an item or of a save: a word of its own, typed by the user, as a
// server writes the name of a tool or of a model among them
export const NAME_PATTERN = /^[A-Za-z0-9][A-Za-z0-9_.:/-]*$/;
// between the endpoint and the model id in the name of a model: prod/qwen3:8b
export const MODEL_SEPARATOR = '/';
// the module holding how far the model changes each module
export const PRIVILEGE = 'privilege';
// the configuration the page runs, and the one it starts with, as IOS names
// them; neither is a save name
export const RUNNING_CONFIG = 'running-config';
export const STARTUP_CONFIG = 'startup-config';
// a word naming one of them by any prefix, as IOS reads copy run start, else
// a save by its name
export const configuration = (word: string): string =>
	[RUNNING_CONFIG, STARTUP_CONFIG].find((n) => n.startsWith(word.toLowerCase())) ?? word;
export const ERROR_PREFIX = '% ';
export const COMMENT_PREFIX = '!';
// a line of output that is not a command: a heading, a note, a warning
export const comment = (text: string): string => `${COMMENT_PREFIX} ${text}`;
// a line of the composer opening with it runs on the CLI
export const SLASH = '/';
// the name of the command line: the head of a command, and what reads in place
// of the title of a conversation the CLI opened, until a message names it
export const CLI = 'CLI';
// the title of a conversation after what names it on a CLI line, quoted as
// title reads it, nothing while the conversation has none
export const titled = (title: string, quote: (value: string) => string): string =>
	title ? ` ${quote(title)}` : '';
// what stands for a secret kept out of view, in the lines the history keeps;
// never a value, as its brackets tell
export const REMOVED = '<removed>';
// optional file next to the page: set lines that become the defaults of a site
export const SITE_CONFIG_URL = 'kiss.conf';
export const PIPE = '|';
export const OUTPUT_MAX_LINES = 200;
// a word this many edits away from a candidate per three letters is a typo
export const TYPO_LETTERS_PER_EDIT = 3;

// mcp: a connection, its handshake and tool list, within MCP_CONNECT_TIMEOUT_MS,
// so a server that answers no more never holds a turn for longer
export const MCP_CONNECT_TIMEOUT_MS = 5000;

// agent
export const TITLE_LENGTH = 60;

// page: how long a copy button shows its check
export const COPIED_MS = 1500;

const two = (n: number) => String(n).padStart(2, '0');

// a date to the minute, in the time of the browser
export function localTime(date: string | number): string {
	const d = new Date(date);
	const day = `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())}`;
	return `${day} ${two(d.getHours())}:${two(d.getMinutes())}`;
}

// the day of a moment on the calendar of the browser, as a count of days
const DAY_MS = 86400000;
export function day(time: number): number {
	const d = new Date(time);
	return Date.UTC(d.getFullYear(), d.getMonth(), d.getDate()) / DAY_MS;
}

// how long ago a moment was, in the language of the browser: now, minutes and
// hours as they pass, then days on the calendar, so yesterday is the day
// before today, then weeks, months and years of them
const PASSING: [Intl.RelativeTimeFormatUnit, number][] = [
	['hour', 3600],
	['minute', 60]
];
const CALENDAR: [Intl.RelativeTimeFormatUnit, number][] = [
	['year', 365],
	['month', 30],
	['week', 7],
	['day', 1]
];
export function ago(time: number, now: number, locale?: string): string {
	const format = new Intl.RelativeTimeFormat(locale, { numeric: 'auto' });
	if (now - time >= DAY_MS) {
		const days = day(now) - day(time);
		const [unit, size] = CALENDAR.find(([, size]) => days >= size)!;
		return format.format(-Math.floor(days / size), unit);
	}
	const seconds = (now - time) / 1000;
	const passed = PASSING.find(([, size]) => seconds >= size);
	return passed
		? format.format(-Math.floor(seconds / passed[1]), passed[0])
		: format.format(0, 'second');
}

// the name of the day of a moment, seen from now: today, yesterday, else its
// date, with its year when that is not this year
export function dayName(time: number, now: number, locale?: string): string {
	const days = day(now) - day(time);
	if (days < 2) {
		return new Intl.RelativeTimeFormat(locale, { numeric: 'auto' }).format(-days, 'day');
	}
	const thisYear = new Date(time).getFullYear() === new Date(now).getFullYear();
	const year = thisYear ? undefined : 'numeric';
	return new Intl.DateTimeFormat(locale, { day: 'numeric', month: 'short', year }).format(time);
}

// a moment in full, to the minute, in the language of the browser
export function stamp(time: number, locale?: string): string {
	return new Intl.DateTimeFormat(locale, { dateStyle: 'medium', timeStyle: 'short' }).format(time);
}

// the words a line holds past its end: what they follow, then themselves
export function beyond(head: readonly string[], rest: readonly string[]): Error {
	return new Error(`nothing goes after ${head.join(' ')}: ${rest.join(' ')}`);
}

// conversations: the word naming every one of them, and their files, named
// after a title without the characters a file name cannot hold
export const ALL = 'all';
// the length an id shows, unique among a few thousand conversations
export const ID_SHOWN = 8;
export const FILE_EXTENSION = '.json';
export const CONFIG_EXTENSION = '.conf';
const UNSAFE_NAME = /[\\/:*?"<>|]/g;
export const fileName = (title: string, extension: string): string =>
	title.replace(UNSAFE_NAME, '-') + extension;

// storage
export const DB_NAME = 'kiss';
export const DB_VERSION = 1;
export const DB_STORE = 'conversations';
// the width of the sidebar as the user last dragged it, in pixels, 0 when closed
export const SIDEBAR_STORAGE_KEY = 'kiss.sidebar';
