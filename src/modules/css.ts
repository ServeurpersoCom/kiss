import type { Key, Module } from '../lib/types.js';
import page from '../tokens.css?raw';

// the properties a token takes the values of, the first its default fits
const PROPERTIES = ['color', 'width', 'font-family'];

// the tokens of the page under :root, each a key by its name past the two
// dashes, typed by its default; a token derived from others, its value
// reading var(), follows them and is no key
function tokens(): Record<string, Key> {
	const sheet = new CSSStyleSheet();
	sheet.replaceSync(page);
	const root = [...sheet.cssRules].find(
		(r): r is CSSStyleRule => r instanceof CSSStyleRule && r.selectorText === ':root'
	)!;
	const keys: Record<string, Key> = {};
	for (const name of Array.from(root.style)) {
		const value = root.style.getPropertyValue(name).trim();
		if (value.includes('var(')) continue;
		const property = PROPERTIES.find((p) => CSS.supports(p, value));
		keys[name.slice(2)] = { kind: 'css', property, default: value };
	}
	return keys;
}

const keys = tokens();

// the tokens the user or the model sets, then the sheets, the last style
// sheets of the document, outside the layer of the page style, so they win
// over every rule of it, a sheet over a token
const set = document.head.appendChild(document.createElement('style'));
const sheet = document.head.appendChild(document.createElement('style'));

// both hold off while a question stands, through a media no screen matches,
// which outlasts any change of their text: nothing restyles or covers the card
// the user answers
export function hold(held: boolean): void {
	set.media = sheet.media = held ? 'not all' : '';
}

export default {
	name: 'css',
	keys: {
		...keys,
		// style sheets over the style of the page, applied by name; a selector
		// reaches anything the thread shows, so the model writes one only as
		// far as its privilege lets it
		sheet: { kind: 'string', named: true, guard: 'change' }
	},
	apply(config) {
		set.textContent = ':root {}';
		const root = set.sheet!.cssRules[0] as CSSStyleRule;
		for (const [key, def] of Object.entries(keys)) {
			const value = config.get(`css ${key}`)!;
			if (value !== def.default) root.style.setProperty(`--${key}`, value);
		}
		sheet.textContent = config
			.names('css')
			.map((name) => String(config.get('css sheet', name)))
			.join('\n');
	}
} satisfies Module;
