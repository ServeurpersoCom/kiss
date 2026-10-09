// the icons of the page, drawn on a grid of 24, stroked at 2 with round ends,
// in the color and at the size of the text around them; stop alone is filled
export const PATHS = {
	send: 'M12 19V5M5 12l7-7 7 7',
	stop: 'M8 8h8v8H8z',
	close: 'M6 6l12 12M18 6L6 18',
	menu: 'M4 6h16M4 12h16M4 18h16',
	chevron: 'M9 6l6 6-6 6',
	previous: 'M15 6l-6 6 6 6',
	copy: 'M9 9h11v11H9zM5 15H4V4h11v1',
	check: 'M5 12l5 5 9-10',
	edit: 'M4 20h4L19 9l-4-4L4 16zM14 6l4 4'
};

export type IconName = keyof typeof PATHS;

// the attributes of the svg of every icon
export function stroke(name: IconName): Record<string, string> {
	return {
		viewBox: '0 0 24 24',
		width: '1em',
		height: '1em',
		fill: name === 'stop' ? 'currentColor' : 'none',
		stroke: 'currentColor',
		'stroke-width': '2',
		'stroke-linecap': 'round',
		'stroke-linejoin': 'round',
		'aria-hidden': 'true'
	};
}
