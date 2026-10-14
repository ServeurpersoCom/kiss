import { DRAFTS_STORAGE_KEY } from './config.js';

// the text being typed in each conversation, by conversation id, as the
// browser keeps it across a reload; the conversation not created yet keys as
// NEW_DRAFT
export const NEW_DRAFT = '';

type Drafts = Record<string, string>;

function read(): Drafts {
	const kept = localStorage.getItem(DRAFTS_STORAGE_KEY);
	return kept ? JSON.parse(kept) : {};
}

function write(drafts: Drafts): void {
	localStorage.setItem(DRAFTS_STORAGE_KEY, JSON.stringify(drafts));
}

export const draftOf = (id: string): string => read()[id] ?? '';

// an empty text keeps no draft
export function keep(id: string, text: string): void {
	const drafts = read();
	if (text) drafts[id] = text;
	else delete drafts[id];
	write(drafts);
}

// the drafts of the conversations named stay, with the one of the conversation
// not created yet
export function prune(ids: readonly string[]): void {
	const drafts = read();
	for (const id of Object.keys(drafts))
		if (id !== NEW_DRAFT && !ids.includes(id)) delete drafts[id];
	write(drafts);
}
