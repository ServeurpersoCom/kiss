import type { ConfigReader, Key, KeyWords, Module, Schema, Value } from '../lib/types.js';
import { Incomplete } from '../lib/types.js';
import { NAME_PATTERN, REMOVED, comment } from '../lib/config.js';
import { didYouMean } from './near.js';

const URL_PROTOCOLS = ['http:', 'https:'];
// a value that reads back as one token without quotes, a pipe opening a filter
const BARE_VALUE = /^[^\s"'|][^\s|]*$/;
const WORD = /^[a-z][a-z0-9_]*(-[a-z0-9]+)*$/;

// a stored key is the key, then the item name for a named key
export function stored(key: string, name?: string): string {
	return name ? `${key} ${name}` : key;
}

// the key and the item name of a stored key
export function unstore(stored: string): [string, string | undefined] {
	const words = stored.split(' ');
	return [words.slice(0, 2).join(' '), words[2]];
}

// the word a typed word names: itself, else the only word it starts
function pick(word: string, words: readonly string[], what: string): string | undefined {
	const typed = word.toLowerCase();
	if (words.includes(typed)) return typed;
	const hits = words.filter((w) => w.startsWith(typed));
	if (hits.length > 1) throw new Error(`ambiguous ${what} "${word}": ${hits.join(' ')}`);
	return hits[0];
}

function check(def: Key, raw: string): Value {
	switch (def.kind) {
		case 'string':
			return raw;
		case 'secret':
			if (!raw) throw new Error('a secret is never empty');
			if (raw === REMOVED)
				throw new Error(
					`"${REMOVED}" stands for a secret kept out of view, it is no value -> the line without a value asks the user`
				);
			return raw;
		case 'number': {
			const n = Number(raw);
			if (!raw.trim() || !Number.isFinite(n)) throw new Error(`"${raw}" is not a number`);
			if (def.integer && !Number.isInteger(n)) throw new Error(`"${raw}" is not a whole number`);
			if (def.min !== undefined && n < def.min) throw new Error(`"${raw}" is below ${def.min}`);
			if (def.max !== undefined && n > def.max) throw new Error(`"${raw}" is above ${def.max}`);
			return String(n);
		}
		case 'enum':
			if (!def.values?.includes(raw)) {
				throw new Error(`"${raw}" is not one of ${def.values?.join(' ')}`);
			}
			return raw;
		case 'url': {
			let url: URL;
			try {
				url = new URL(raw);
			} catch {
				throw new Error(`"${raw}" is not a URL`);
			}
			if (!URL_PROTOCOLS.includes(url.protocol)) throw new Error(`"${raw}" is not http or https`);
			return raw.replace(/\/+$/, '');
		}
		case 'css':
			if (!CSS.supports(def.property!, raw)) throw new Error(`"${raw}" is not a ${def.property}`);
			return raw;
	}
}

// what is wrong with the declaration of a key, if anything
function audit(key: string, def: Key): string | null {
	if (!key.split(' ').every((w) => WORD.test(w))) return `"${key}": bad key name`;
	if (def.kind === 'enum' && !def.values?.length) return `${key}: enum without values`;
	if (def.guard === 'opening' && def.kind !== 'enum') return `${key}: opening guard on no enum`;
	for (const value of [def.default, ...Object.values(def.defaults ?? {})]) {
		if (value === undefined) continue;
		try {
			check(def, value);
		} catch (e) {
			return `${key}: bad default, ${(e as Error).message}`;
		}
	}
	return null;
}

// the keys every module declares; a key declared twice or declared wrong stops
// the page at once
export class KeySchema implements Schema {
	private modules: Map<string, Record<string, Key>>;
	private entries: [string, Key][];

	constructor(private all: readonly Module[]) {
		this.modules = new Map(all.map((m) => [m.name, m.keys]));
		this.entries = all
			.flatMap((m) =>
				Object.entries(m.keys).map(([k, def]): [string, Key] => [`${m.name} ${k}`, def])
			)
			.sort(([a], [b]) => a.localeCompare(b));
		const problems = this.entries.map(([key, def]) => audit(key, def));
		const keys = this.entries.map(([key]) => key);
		keys.forEach((k, i) => keys.indexOf(k) !== i && problems.push(`${k}: declared twice`));
		const found = problems.filter((p) => p !== null);
		if (found.length) throw new Error(['bad keys', ...found].join('\n'));
	}

	find(key: string): Key | undefined {
		return this.entries.find(([k]) => k === key)?.[1];
	}

	list(): [string, Key][] {
		return this.entries;
	}

	parse(key: string, raw: string): Value {
		const def = this.find(key);
		if (!def) throw new Error(`unknown key "${key}"`);
		try {
			return check(def, raw);
		} catch (e) {
			throw new Error(`${key}: ${(e as Error).message}`);
		}
	}

	quote(value: Value): string {
		return BARE_VALUE.test(value) ? value : JSON.stringify(value);
	}

	module(word: string): string {
		const names = [...this.modules.keys()].sort();
		const module = pick(word, names, 'key');
		if (word.includes('.')) {
			throw new Error(`unknown key "${word}", did you mean ${word.split('.').join(' ')}`);
		}
		if (!module) throw new Error(`unknown key "${word}"` + didYouMean(word, names));
		return module;
	}

	key(module: string, word: string): string | undefined {
		return pick(word, Object.keys(this.modules.get(module) ?? {}).sort(), `${module} key`);
	}

	collection(module: string): boolean {
		return Object.values(this.modules.get(module) ?? {}).some((def) => def.named);
	}

	read(words: readonly string[]): KeyWords {
		if (!words.length) throw new Incomplete();
		const module = this.module(words[0]);
		if (words.length < 2) throw new Incomplete();
		const keyWord = this.key(module, words[1]);
		if (!keyWord) {
			const keys = Object.keys(this.modules.get(module) ?? {});
			throw new Error(
				`unknown key "${module} ${words[1]}"` + didYouMean(words[1], keys, `${module} `)
			);
		}
		const key = `${module} ${keyWord}`;
		const def = this.find(key)!;
		if (!def.named) return { key, def, module, rest: words.slice(2) };
		if (words.length < 3) throw new Incomplete();
		if (!NAME_PATTERN.test(words[2])) throw new Error(`"${words[2]}" is not a ${module} name`);
		const names = def.names?.(this.all);
		if (names && !names.includes(words[2])) {
			throw new Error(`unknown ${key} "${words[2]}"` + didYouMean(words[2], names));
		}
		return { key, def, module, name: words[2], rest: words.slice(3) };
	}

	next(config: ConfigReader, words: readonly string[], value: boolean, items: boolean): string[] {
		try {
			if (!words.length) return [...this.modules.keys()].sort();
			const module = this.module(words[0]);
			const keys = Object.keys(this.modules.get(module) ?? {}).sort();
			if (words.length === 1) {
				return items && this.collection(module) ? [...keys, ...config.names(module)] : keys;
			}
			const keyWord = this.key(module, words[1]);
			if (!keyWord) return [];
			const def = this.find(`${module} ${keyWord}`)!;
			if (def.named && words.length === 2) {
				return def.names ? [...def.names(this.all)] : [...config.names(module), '<name>'];
			}
			if (!value || words.length !== (def.named ? 3 : 2)) return [];
			if (def.kind === 'enum') return [...(def.values ?? [])];
			return [`<${def.property ?? def.kind}>`];
		} catch {
			return [];
		}
	}

	unstore(stored: string): [string, string | undefined] {
		return unstore(stored);
	}

	known(stored: string): boolean {
		const [key, name] = unstore(stored);
		const def = this.find(key);
		if (!def || !!def.named !== !!name) return false;
		return !name || !def.names || def.names(this.all).includes(name);
	}

	// what a change does, line by line: the old line after -, the new one after
	// +; a secret replaced by another reads as one line, its values out of view
	diff(from: Record<string, Value | undefined>, to: Record<string, Value | undefined>): string[] {
		const lines: string[] = [];
		for (const k of [...new Set([...Object.keys(from), ...Object.keys(to)])].sort()) {
			if (from[k] === to[k]) continue;
			if (from[k] !== undefined && to[k] !== undefined && this.secret(k)) {
				lines.push(comment(`${k} changed`));
				continue;
			}
			if (from[k] !== undefined) lines.push('- ' + this.line(k, from[k]));
			if (to[k] !== undefined) lines.push('+ ' + this.line(k, to[k]));
		}
		return lines;
	}

	secret(stored: string): boolean {
		return this.find(unstore(stored)[0])?.kind === 'secret';
	}

	line(stored: string, value: Value): string {
		if (this.secret(stored)) return comment(`${stored} is set`);
		return `set ${stored} ${this.quote(value)}`;
	}
}
