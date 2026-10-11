// the arguments of a call as far as the stream has written them, in the order
// the model wrote them; none while nothing reads as a JSON object
export function partial(json: string): [string, unknown][] {
	for (const text of closings(json)) {
		let parsed: unknown;
		try {
			parsed = JSON.parse(text);
		} catch {
			continue;
		}
		return parsed && typeof parsed === 'object' && !Array.isArray(parsed)
			? Object.entries(parsed)
			: [];
	}
	return [];
}

// two ways to close a JSON cut short: whole, its open string and brackets
// closed, which reads when the cut falls inside a value; else back to its
// last comma or bracket, which reads when the cut falls in a key, after a
// colon or inside a literal
function closings(json: string): [string, string] {
	let close = '';
	let cut = 0;
	let rest = '';
	let string = false;
	let escaped = false;
	for (let i = 0; i < json.length; i++) {
		const ch = json[i];
		if (string) {
			if (escaped) escaped = false;
			else if (ch === '\\') escaped = true;
			else if (ch === '"') string = false;
			continue;
		}
		if (ch === '"') {
			string = true;
			continue;
		}
		if (ch === '{' || ch === '[') close = (ch === '{' ? '}' : ']') + close;
		else if (ch === '}' || ch === ']') close = close.slice(1);
		else if (ch !== ',') continue;
		cut = ch === ',' ? i : i + 1;
		rest = close;
	}
	const whole = (escaped ? json.slice(0, -1) : json) + (string ? '"' : '') + close;
	return [whole, json.slice(0, cut) + rest];
}
