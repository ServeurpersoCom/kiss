// the name the page shows
export const NAME = 'KiSS';
// the system, as a router names its own; a KiSS server gives it as its title
export const SYSTEM = `${NAME} Operating System`;
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
// a line of the page to the user or the model, its first letter capital, but
// for one that opens on a URL, written as it is
const URL_START = /^[a-z][a-z0-9+.-]*:\/\//;
export const capital = (text: string): string =>
	URL_START.test(text) ? text : text.charAt(0).toUpperCase() + text.slice(1);
// a line of the composer opening with it runs on the CLI
export const SLASH = '/';
// the name of the command line: the head of a command, and what reads in place
// of the title of a conversation the CLI opened, until a message names it
export const CLI = 'CLI';
// the title of a conversation after what names it on a CLI line, quoted as
// title reads it, nothing while the conversation has none
export const titled = (title: string, quote: (value: string) => string): string =>
	title ? ` ${quote(title)}` : '';
// the factory configuration the page carries, and the optional file next to
// the page that a site writes over it: set lines both
export const FACTORY_FILE = 'factory client.conf';
export const SITE_CONFIG_URL = 'client.conf';
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

// conversations: what export and erase name for every one of them, the name of
// the file one conversation goes to, and their files, named after a title
// without the characters a file name cannot hold
export const CONVERSATIONS = 'conversations';
export const CONVERSATION = 'conversation';
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
// the text being typed in each conversation, as the composer left it
export const DRAFTS_STORAGE_KEY = 'kiss.drafts';
// whether the page is attached to its server, on or off, as attach or detach
// left it; none while neither ran
export const ATTACHED_STORAGE_KEY = 'kiss.attached';
// the hostname the page draws the first time it opens, its default ever since
export const HOSTNAME_STORAGE_KEY = 'kiss.hostname';
