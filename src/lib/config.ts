// the name the page shows
export const NAME = 'KiSS';

// engine
export const ARCHIVE_STORAGE_KEY = 'kiss.saves';
// a name of an item or of a save: a word of its own, typed by the user, as a
// server writes the name of a tool or of a model among them
export const NAME_PATTERN = /^[A-Za-z0-9][A-Za-z0-9_.:/-]*$/;
// between the endpoint and the model id in the name of a model: prod/qwen3:8b
export const MODEL_SEPARATOR = '/';
// the module holding how far the model changes each module
export const PRIVILEGE = 'privilege';
// the word show diff reads as the running configuration, never a save name
export const SESSION = 'session';
export const ERROR_PREFIX = '% ';
export const COMMENT_PREFIX = '!';
// a line of output that is not a command: a heading, a note, a warning
export const comment = (text: string): string => `${COMMENT_PREFIX} ${text}`;
// a line of the composer opening with it runs on the CLI
export const SLASH = '/';
export const SECRET_SET = '<set>';
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

// how long ago a moment was, in the language of the browser: now, then the
// largest unit it counts whole, yesterday and last week included
const UNITS: [Intl.RelativeTimeFormatUnit, number][] = [
	['year', 31536000],
	['month', 2592000],
	['week', 604800],
	['day', 86400],
	['hour', 3600],
	['minute', 60]
];
export function ago(time: number, now: number, locale?: string): string {
	const seconds = (now - time) / 1000;
	const format = new Intl.RelativeTimeFormat(locale, { numeric: 'auto' });
	for (const [unit, size] of UNITS) {
		if (seconds >= size) return format.format(-Math.floor(seconds / size), unit);
	}
	return format.format(0, 'second');
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
const UNSAFE_NAME = /[\\/:*?"<>|]/g;
export const fileName = (title: string): string => title.replace(UNSAFE_NAME, '_') + FILE_EXTENSION;

// storage
export const DB_NAME = 'kiss';
export const DB_VERSION = 1;
export const DB_STORE = 'conversations';
// the width of the sidebar as the user last dragged it, in pixels, 0 when closed
export const SIDEBAR_STORAGE_KEY = 'kiss.sidebar';
