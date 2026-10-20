import type { Assistant, Message, Round } from './types.js';
import prompts from '../prompts.json';

// the compaction of a history: once a request holds as many tokens as the
// trigger of its model, the steps the model reads but the preserved last ones
// fold into a summary, a round of the turn the next request reads instead of
// them; a step is what the model reads of the history, a user message or one
// round of an assistant turn; the history keeps every step, and what the model
// reads always follows from it alone

// the keys of a model that KiSS reads itself, never sent
export const TRIGGER = 'compaction-trigger-tokens';
export const PRESERVE = 'compaction-preserve-steps';

// where the summary goes in the text the model reads it in
const SUMMARY = '{summary}';

// a step by where it sits: a user message, or one round of an assistant turn
interface Step {
	message: number;
	round?: number;
}

// what the model reads of a history: the last summary, none before the first,
// and the steps after it, the kept ones before it first
interface View {
	summary: string | null;
	steps: Step[];
}

// whether a round is a summary
const summary = (r: Round): boolean => r.kept !== undefined;

// every step of a history in order, a summary or a cli message being none
function every(messages: readonly Message[]): Step[] {
	return messages.flatMap((m, message): Step[] => {
		if (m.role === 'user') return [{ message }];
		if (m.role === 'cli') return [];
		return m.rounds.flatMap((r, round) => (summary(r) ? [] : [{ message, round }]));
	});
}

// the steps the model reads: those after the last summary, behind the kept
// steps right before it
function view(messages: readonly Message[]): View {
	const all = every(messages);
	for (let message = messages.length - 1; message >= 0; message--) {
		const m = messages[message];
		if (m.role !== 'assistant') continue;
		for (let round = m.rounds.length - 1; round >= 0; round--) {
			const r = m.rounds[round];
			if (!summary(r)) continue;
			const after = all.findIndex(
				(s) => s.message > message || (s.message === message && s.round! > round)
			);
			const at = after < 0 ? all.length : after;
			return { summary: r.text, steps: all.slice(Math.max(0, at - r.kept!)) };
		}
	}
	return { summary: null, steps: all };
}

// the history a view reads as: the summary as a user message, then the steps,
// the rounds of one turn in a row as one assistant message
function render(messages: readonly Message[], v: View): Message[] {
	const out: Message[] = [];
	if (v.summary !== null) {
		out.push({ role: 'user', text: prompts.compaction.summary.replace(SUMMARY, v.summary) });
	}
	// the assistant message the rounds of the turn read now go into
	let reply: Assistant | null = null;
	let turn = -1;
	for (const s of v.steps) {
		const m = messages[s.message];
		if (m.role !== 'assistant') {
			out.push(m);
			reply = null;
			continue;
		}
		if (!reply || turn !== s.message) {
			reply = { role: 'assistant', rounds: [] };
			turn = s.message;
			out.push(reply);
		}
		reply.rounds.push(m.rounds[s.round!]);
	}
	return out;
}

// the history as the model reads it
export function compacted(messages: readonly Message[]): Message[] {
	return render(messages, view(messages));
}

// what a summary of the history asks the model, the steps it folds then the
// ask, and how many steps it keeps; none when the steps it reads fold none
// past the preserved ones
export function folding(
	messages: readonly Message[],
	preserve: number
): { messages: Message[]; kept: number } | null {
	const v = view(messages);
	const folded = v.steps.length - preserve;
	if (folded < 1) return null;
	return {
		messages: [
			...render(messages, { summary: v.summary, steps: v.steps.slice(0, folded) }),
			{ role: 'user', text: prompts.compaction.ask }
		],
		kept: preserve
	};
}
