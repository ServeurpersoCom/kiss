// a text field as tall as the lines it holds
export function fit(field: HTMLTextAreaElement): void {
	field.style.height = 'auto';
	field.style.height = `${field.scrollHeight}px`;
}
