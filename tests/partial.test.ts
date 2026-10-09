import { describe, expect, it } from 'vitest';
import { partial } from '../src/lib/partial.js';

// every prefix of a call as the stream writes it
const prefixes = (json: string): string[] => [...json].map((_, i) => json.slice(0, i + 1));

describe('the arguments of a call as they stream', () => {
	it('read whole once written', () => {
		expect(partial('{"command":"show running","n":2}')).toEqual([
			['command', 'show running'],
			['n', 2]
		]);
	});
	it('grow with a string value cut anywhere', () => {
		expect(partial('{"command":"show ru')).toEqual([['command', 'show ru']]);
		expect(partial('{"command":"a\\')).toEqual([['command', 'a']]);
		expect(partial('{"command":"a\\"b')).toEqual([['command', 'a"b']]);
	});
	it('hold the written ones while a key, a colon or a literal is cut', () => {
		const first: [string, unknown][] = [['command', 'ls']];
		expect(partial('{"command":"ls","pa')).toEqual(first);
		expect(partial('{"command":"ls","path":')).toEqual(first);
		expect(partial('{"command":"ls","all":tr')).toEqual(first);
		expect(partial('{"command":"ls",')).toEqual(first);
	});
	it('close nested brackets', () => {
		expect(partial('{"a":{"b":[1,{"c":"d')).toEqual([['a', { b: [1, { c: 'd' }] }]]);
		expect(partial('{"a":{"b":1},"c')).toEqual([['a', { b: 1 }]]);
	});
	it('never lose the first argument once it shows', () => {
		const json = '{"command":"show running | include x","depth":{"n":[1,2]},"ok":true}';
		let seen = false;
		for (const p of prefixes(json)) {
			const all = partial(p);
			if (all.length) seen = true;
			else expect(seen).toBe(false);
		}
		expect(seen).toBe(true);
	});
	it('read none from nothing or what is not an object', () => {
		expect(partial('')).toEqual([]);
		expect(partial('{"')).toEqual([]);
		expect(partial('[1,2]')).toEqual([]);
		expect(partial('nope')).toEqual([]);
	});
});
