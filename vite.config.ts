/// <reference types="vitest/config" />
import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { execSync } from 'child_process';
import type { Plugin as CssPlugin } from 'postcss';

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

// every style of the page sits in one cascade layer, so theme css, outside any
// layer, wins over it whatever the specificity of its selectors
const pageLayer: CssPlugin = {
	postcssPlugin: 'kiss-page-layer',
	Once(root, { AtRule }) {
		root.append(new AtRule({ name: 'layer', params: 'page' }).append(root.nodes));
	}
};

export default defineConfig({
	plugins: [svelte(), viteSingleFile(), katexWoff2Only()],

	css: {
		postcss: { plugins: [pageLayer] }
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
