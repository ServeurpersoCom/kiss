import type { Module } from '../lib/types.js';

// the last style sheet of the document, outside the layer of the page style, so
// it wins over every rule of it
const sheet = document.head.appendChild(document.createElement('style'));

export default {
	name: 'css',
	keys: {
		// style sheets over the style of the page, applied by name
		sheet: { kind: 'string', named: true }
	},
	apply(config) {
		sheet.textContent = config
			.names('css')
			.map((name) => String(config.get('css sheet', name)))
			.join('\n');
	}
} satisfies Module;
