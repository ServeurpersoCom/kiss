import type { Assistant, Conversation } from './types.js';
import type { Grant, Verdict } from '../engine/engine.js';

// the state of the page that every view reads: the conversations, the open
// one, the turns written now, the questions to the user, the sidebar

// what the page asks the user, and how the answer settles it: a change or a
// call the model asks for, the value of a secret key, a file to save, a file
// to read, or a yes
export type Asking =
	| { kind: 'grant'; request: Grant; settle(verdict: Verdict): void }
	| { kind: 'secret'; key: string; note?: string; settle(value: string | null): void }
	| { kind: 'offer'; name: string; text: string; settle(saved: boolean): void }
	| { kind: 'pick'; settle(text: string | null): void }
	| { kind: 'confirm'; question: string; settle(yes: boolean): void };
// a question as it waits for the user, numbered, with the conversation it
// comes from
export type Posed = Asking & { id: number; from: string };

export const app = $state({
	conversations: [] as Conversation[],
	current: null as Conversation | null,
	// the turns the model writes now, by conversation
	replies: {} as Record<string, Assistant>,
	// the questions to the user in the order they came, each conversation
	// showing its first one, as each is answered in turn
	asks: [] as Posed[],
	// the conversation list, open over the thread on a narrow screen
	sidebar: false
});
