// setup.ts: what every test file runs in before its imports

// the page runs in standards mode, which happy-dom does not report and KaTeX
// checks as it loads
Object.defineProperty(document, 'compatMode', { value: 'CSS1Compat' });
