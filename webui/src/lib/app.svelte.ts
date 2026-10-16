import type { Assistant, Conversation } from './types.js';
import type { Grant, Verdict } from '../engine/engine.js';
import { hold } from '../modules/style.js';

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

// the number of the last question posed
let posed = 0;

// a question to the user from a conversation, shown in it once those it asked
// before are answered; the style tokens and sheets hold off while any question
// stands; spent takes the time it stood once it is answered
export function pose<T>(
	from: string,
	ask: (settle: (answer: T) => void) => Asking,
	spent?: (ms: number) => void
): Promise<T> {
	return new Promise((resolve) => {
		const id = ++posed;
		const asked = performance.now();
		hold(true);
		// the first answer settles it, any later one finds it gone
		const settle = (answer: T) => {
			if (!app.asks.some((q) => q.id === id)) return;
			withdraw(id);
			spent?.(performance.now() - asked);
			resolve(answer);
		};
		app.asks.push({ ...ask(settle), id, from });
	});
}

// a question leaves the page, answered or taken back, the style held while
// another stands
export function withdraw(id: number): void {
	app.asks = app.asks.filter((q) => q.id !== id);
	hold(app.asks.length > 0);
}

// the number of the last question posed, the one pose just showed
export const last = (): number => posed;
