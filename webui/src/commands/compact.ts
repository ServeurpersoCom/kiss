import type { Command } from '../engine/engine.js';

// the conversation the batch runs in compacts its history once the batch
// holds: the steps the model reads but the preserved last ones fold into a
// summary, as they do once a request reaches the trigger of the model; typed
// by the user, a turn of its own writes the summary, run by the model, it
// folds before its next request
export default {
	path: ['compact'],
	roles: ['user', 'llm'],
	run(ctx) {
		if (!ctx.conversation) throw new Error('no conversation here');
		ctx.compact = true;
		return '';
	}
} satisfies Command;
