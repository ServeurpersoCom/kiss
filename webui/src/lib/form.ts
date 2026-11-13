import type { InputRequest } from '@modelcontextprotocol/client';
import type { ToolContext } from './types.js';
import { learn } from '../engine/keychain.js';

// the one input a server may ask of KiSS: a form whose fields are secrets,
// text each typed in the masked field of the page, or yes or no, the user
// confirming at once all that the server says; the user answers them all, or
// leaving one cancels the form
const ELICITATION = 'elicitation/create';
const FORM_MODE = 'form';
const TEXT_FIELD = 'string';
const YES_FIELD = 'boolean';
const ACTIONS = ['accept', 'cancel'] as const;
const [ACCEPT, CANCEL] = ACTIONS;

// what the user answers to a form of a server under its message, field by
// field: a text field typed out of the sight of the model, the yes or no
// fields confirmed together by one answer where the first stands; an input
// other than a form of such fields is none KiSS answers
export async function fill(
	ctx: Pick<ToolContext, 'secret' | 'confirm'>,
	server: string,
	input: InputRequest
): Promise<object> {
	const params = (input.params ?? {}) as {
		mode?: string;
		message?: string;
		requestedSchema?: { properties?: Record<string, { type?: string }> };
	};
	if (input.method !== ELICITATION || (params.mode ?? FORM_MODE) !== FORM_MODE) {
		throw new Error(`asks for ${input.method} ${params.mode ?? ''}, which KiSS does not answer`);
	}
	const fields = Object.entries(params.requestedSchema?.properties ?? {});
	if (!fields.length) throw new Error('asks for a form with no field');
	const other = fields.find(
		([, schema]) => schema.type !== TEXT_FIELD && schema.type !== YES_FIELD
	);
	if (other)
		throw new Error(`asks for ${other[0]}, a field KiSS does not answer: only text or yes`);
	const content: Record<string, string | boolean> = {};
	const note = params.message ?? '';
	const yes = fields.filter(([, schema]) => schema.type === YES_FIELD).map(([field]) => field);
	for (const [field, schema] of fields) {
		if (schema.type === YES_FIELD) {
			if (field !== yes[0]) continue;
			if (!(await ctx.confirm(note || [`Allow this of ${server}?`, ...yes].join('\n'))))
				return { action: CANCEL };
			for (const each of yes) content[each] = true;
			continue;
		}
		const value = await ctx.secret(`${field} of ${server}`, note);
		if (value === null) return { action: CANCEL };
		learn(value);
		content[field] = value;
	}
	return { action: ACCEPT, content };
}
