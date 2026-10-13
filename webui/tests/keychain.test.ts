import { afterEach, describe, expect, it } from 'vitest';
import type { ConfigReader } from '../src/lib/types.js';
import { credential, hold, learn, placement, seal } from '../src/lib/keychain.js';

// a configuration that holds one key, sk-1, for item a of endpoints
const config = {
	reveal: (key: string, name?: string) =>
		key === 'endpoints key' && name === 'a' ? 'sk-1' : undefined
} as ConfigReader;

afterEach(() => hold(() => []));

describe('the keychain', () => {
	it('places a key as Header or Header: Scheme, none without a key', () => {
		expect(credential(config, 'Authorization: Bearer', 'endpoints', 'a')).toEqual({
			Authorization: 'Bearer sk-1'
		});
		expect(credential(config, 'api-key', 'endpoints', 'a')).toEqual({ 'api-key': 'sk-1' });
		expect(credential(config, 'x-api-key', 'endpoints', 'b')).toEqual({});
		expect(placement('a/b')).toBeNull();
		expect(placement('Authorization: two words')).toBeNull();
	});

	it('seals every string a value holds, a secret escaped or in pieces read whole', () => {
		hold(() => ['sk-proj-0001']);
		learn('typed-0002');
		const sse = ['sk-pr', 'oj-0', '001'].join('');
		const escaped = JSON.parse('"\\u0073k-proj-0001"');
		const sealed = seal({ a: escaped, b: [sse, 'x typed-0002 y'], c: 7 });
		expect(JSON.parse(sealed)).toEqual({ a: '<removed>', b: ['<removed>', 'x <removed> y'], c: 7 });
	});

	it('seals the name of a property as its value', () => {
		hold(() => ['sk-proj-0001']);
		const schema = { type: 'object', properties: { 'sk-proj-0001': { type: 'string' } } };
		expect(seal(schema)).not.toContain('sk-proj-0001');
		expect(JSON.parse(seal(schema)).properties).toEqual({ '<removed>': { type: 'string' } });
	});

	it('keeps a secret it has known, a key replaced since included', () => {
		hold(() => ['sk-old-0003']);
		seal('');
		hold(() => ['sk-new-0004']);
		expect(JSON.parse(seal('sk-old-0003 sk-new-0004'))).toBe('<removed> <removed>');
	});

	it('is the one reader of a key beside the engine that holds it', () => {
		const files = import.meta.glob<string>('../src/**/*.{ts,svelte}', {
			query: '?raw',
			import: 'default',
			eager: true
		});
		const readers = Object.keys(files).filter((f) => /\.reveal\(/.test(files[f]));
		expect(readers.filter((f) => !/\/(engine|commands)\/|\/lib\/keychain\.ts$/.test(f))).toEqual(
			[]
		);
		expect(readers).toContain('../src/lib/keychain.ts');
	});

	it('takes a secret that holds another whole', () => {
		hold(() => ['sk-1', 'sk-1-long']);
		expect(JSON.parse(seal('a sk-1-long b sk-1'))).toBe('a <removed> b <removed>');
	});
});
