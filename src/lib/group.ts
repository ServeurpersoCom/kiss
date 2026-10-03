import type { Group } from './types.js';

// the lines of groups, each body under its header: a failed group holds its
// error on the header, an empty one shows nothing
export function grouped(groups: Group[], body: (name: string) => string[]): string[] {
	return groups.flatMap((g) => {
		if (g.error) return [`! ${g.group} ${g.error}`];
		const lines = g.names.flatMap(body);
		return lines.length ? [`! ${g.group}`, ...lines] : [];
	});
}
