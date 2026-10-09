import type { Tool } from './types.js';

// the tools of KiSS itself: a file in tools/ offers its tool by existing
const files = import.meta.glob<{ default: Tool }>('../tools/*.ts', { eager: true });

export const tools: readonly Tool[] = Object.values(files).map((f) => f.default);

// whether the model sees a tool
export const USES = ['on', 'off'] as const;
export const [ON, OFF] = USES;
