// the time the page reads how long ago a message entered from, a minute at most
// behind
const MINUTE_MS = 60000;

export const clock = $state({ now: Date.now() });

setInterval(() => (clock.now = Date.now()), MINUTE_MS);
