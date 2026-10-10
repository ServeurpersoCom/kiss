/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { execSync } from 'child_process';
import type { Plugin as CssPlugin } from 'postcss';
import { scope } from './src/lib/config';

// git version baked at build time
function gitVersion(): string {
	try {
		const hash = execSync('git rev-parse --short HEAD', { cwd: __dirname }).toString().trim();
		const date = execSync('git show -s --format=%cs HEAD', { cwd: __dirname }).toString().trim();
		return `${hash} (${date})`;
	} catch {
		return 'unknown';
	}
}

// KaTeX lists each font as woff2, woff and ttf; every browser KiSS runs on reads
// woff2, so only those get inlined into the page
function katexWoff2Only() {
	return {
		name: 'kiss:katex-woff2',
		enforce: 'pre' as const,
		transform(code: string, id: string) {
			if (!id.includes('katex') || !id.endsWith('.css')) return null;
			return code.replace(/,\s*url\([^)]*\.(woff|ttf)\) format\("(woff|truetype)"\)/g, '');
		}
	};
}

// every style sits in a cascade layer by where it comes from: the libraries
// of node_modules in lib, the page itself in page over them; css sheets,
// outside any layer, win over both whatever the specificity of their selectors
const layers: CssPlugin = {
	postcssPlugin: 'kiss-layers',
	Once(root, { AtRule }) {
		const name = root.source?.input.file?.includes('/node_modules/') ? 'lib' : 'page';
		root.append(new AtRule({ name: 'layer', params: name }).append(root.nodes));
		root.prepend(new AtRule({ name: 'layer', params: 'lib, page' }));
	}
};

export default defineConfig({
	plugins: [svelte({ compilerOptions: { cssHash: scope } }), viteSingleFile(), katexWoff2Only()],

	css: {
		postcss: { plugins: [layers] }
	},

	define: {
		__KISS_VERSION__: JSON.stringify(gitVersion())
	},

	build: {
		assetsInlineLimit: Infinity,
		cssCodeSplit: false
	},

	// the laws of the engine and the agent, in a browser like page
	test: {
		environment: 'happy-dom',
		// a time zone ahead of UTC, so a law tells local time from UTC
		env: { TZ: 'Europe/Paris' },
		include: ['tests/*.test.ts']
	}
});
