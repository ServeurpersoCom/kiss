// the name the page shows
export const NAME = 'KiSS';

// engine
export const ARCHIVE_STORAGE_KEY = 'kiss.saves';
// a name of an item or of a save: a word of its own, typed by the user, a tool
// name of an MCP server among them
export const NAME_PATTERN = /^[A-Za-z0-9][A-Za-z0-9_.-]*$/;
// the word show diff reads as the running configuration, never a save name
export const SESSION = 'session';
export const ERROR_PREFIX = '% ';
export const COMMENT_PREFIX = '!';
export const SECRET_SET = '<set>';
// the longest a model list may take, whoever asks for it
export const MODELS_TIMEOUT_MS = 5000;
// optional file next to the page: set lines that become the defaults of a site
export const SITE_CONFIG_URL = 'kiss.conf';
export const PIPE = '|';
export const OUTPUT_MAX_LINES = 200;
// a word this many edits away from a candidate per three letters is a typo
export const TYPO_LETTERS_PER_EDIT = 3;

// mcp: a connection, its handshake and tool list, within MCP_CONNECT_TIMEOUT_MS;
// a call within MCP_CALL_TIMEOUT_MS or until a stop, the SDK default being one
// minute
export const MCP_CONNECT_TIMEOUT_MS = 5000;
export const MCP_CALL_TIMEOUT_MS = 300000;

// agent
export const MAX_ROUNDS = 25;
export const TITLE_LENGTH = 60;

// storage
export const DB_NAME = 'kiss';
export const DB_VERSION = 1;
export const DB_STORE = 'conversations';
// the width of the sidebar as the user last dragged it, in pixels, 0 when closed
export const SIDEBAR_STORAGE_KEY = 'kiss.sidebar';
