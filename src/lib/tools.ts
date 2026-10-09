import type { Tool } from './types.js';

// the tools of KiSS itself: a file in tools/ offers its tool by existing
const files = import.meta.glob<{ default: Tool }>('../tools/*.ts', { eager: true });

export const tools: readonly Tool[] = Object.values(files).map((f) => f.default);

// whether the model sees a tool, and calls it freely, from the most closed
export const USES = ['off', 'consent', 'on'] as const;
export const [OFF, CONSENT, ON] = USES;
