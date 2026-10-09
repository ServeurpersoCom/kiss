import type { Command } from '../lib/types.js';
import { comment } from '../lib/config.js';

const two = (n: number) => String(n).padStart(2, '0');

// the date of a save to the minute, in the time of the browser
function local(iso: string): string {
	const d = new Date(iso);
	const day = `${d.getFullYear()}-${two(d.getMonth() + 1)}-${two(d.getDate())}`;
	return `${day} ${two(d.getHours())}:${two(d.getMinutes())}`;
}

export default {
	path: ['show', 'saves'],
	roles: ['user', 'llm'],
	// oldest first, the latest save last: the one the next page load starts with
	run(ctx) {
		const saves = ctx.archive.list();
		if (!saves.length) return comment('nothing saved yet');
		return saves.map((s) => `${s.name} ${local(s.date)}`).join('\n');
	}
} satisfies Command;
