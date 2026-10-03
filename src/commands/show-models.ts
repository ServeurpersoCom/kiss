import type { Command, Group } from '../lib/types.js';
import { listModels } from '../lib/api.js';
import { grouped } from '../lib/group.js';
import { remotes } from '../lib/remote.js';

export default {
	path: ['show', 'models'],
	roles: ['user', 'llm'],
	// the models of every endpoint at once, grouped by endpoint name, each
	// written endpoint/model as chat model takes it
	async run(ctx) {
		const all = remotes(ctx.config, 'endpoints');
		if (!all.length) throw new Error('no endpoint yet: set endpoints url <name> <url>');
		const groups = await Promise.all(
			all.map((e) =>
				listModels(e.url, e.key, ctx.signal).then(
					(ids): Group => ({ group: e.name, names: ids.map((id) => `${e.name}/${id}`) }),
					(err: Error): Group => ({ group: e.name, names: [], error: err.message })
				)
			)
		);
		return grouped(groups, (name) => [name]).join('\n');
	}
} satisfies Command;
