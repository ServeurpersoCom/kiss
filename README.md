# KiSS

Point it at an LLM and just talk.

KiSS is a chat UI with no settings panel. The model configures everything through one
built-in tool: a CLI with one running configuration and as many dated saved configurations
as you want; a configuration that fails its checks is never saved.

## The CLI

The user types it after a `/`, the model sends it through its tool, and the developer terminal
(Ctrl+`) runs it with the rights of the model.

```
batch   = line { newline line }              a line opening with ! is a comment
line    = command { "|" filter }
command = "set" key value
        | "no" key | "no" module item
        | "show" module [ word [ item ] ] | "show" module item
        | "show" ( "running" | "style" | "saves" | "version" )
        | "show diff" save save              the word session names the running one
        | "load" save | "reset"
        | "save" save | "no save" save        user only, each alone on its line
key     = module word [ item ]               an item names one of a collection
save    = name                               a save of the same name gives way
value   = bare | "json string" | 'literal'
filter  = ( "include" | "exclude" | "begin" ) pattern | "count"
```

Its laws, each one held by a test in `tests/`:

- A command, module, key or filter word may be any prefix that names one word only; an exact
  word always wins. A collection is named in the plural, `endpoints`, `tools`, `saves`, `models`,
  but an acronym, `css`, `mcp`: its singular, a prefix of it, names it too.
- A batch compiles all its lines, then runs them on a copy of the running configuration that
  replaces it only when every line succeeds, and answers with what changed.
- One batch runs at a time, whoever sends it. Once a turn is stopped, a batch of the model
  neither starts, nor runs another line, nor replaces anything.
- A value resolves from what the session sets, over `kiss.conf`, over the default of its key;
  `reset` drops what the session sets, `load` puts back the save it names.
- The word after a collection names a key when it names one, else an item, for `no` and `show`
  alike.
- On an item holding a secret, the model sets the secret only: a secret goes to a URL the user
  or the site chose, never to one the model chose.
- The latest save is the configuration the next page load starts with, and the archive changes
  only once the browser stores it.
- A model list answers within its timeout, so no endpoint holds the queue; every endpoint lists
  at once, one that fails beside the others.
- The sheets of `css` apply by name over every style of the page, whatever their selectors.
- A listing from many sources goes by group, `! <group>` over its lines, `! <group> <error>`
  alone when the source fails: `show models` by endpoint, `show tools` by who serves it.
- A turn keeps, on the page and in the browser, what settled: what streamed and the calls that
  ended before a stop, never one that did not.
- Every MCP server serves its tools under their own names, after those of KiSS; a tool turned off
  is never seen by the model; a server that does not answer serves nothing, and a name already
  served stays with the first: the model is told, the save warns of it, nothing ever stops.

## Style

The page style is plain CSS, its tokens on `:root`: colors, `--radius`, `--font`, `--mono`,
`--font-large` for the chat and `--font-small` for everything around it, the only two sizes
but `--font-name`, the name of the page,
`--width`, `--bubble-width`, `--sidebar-width` within `--sidebar-min` and `--sidebar-max`.
`show style` lists it, one rule per line; named sheets restyle anything over it. The sidebar follows
its edge for the whole drag, within those bounds, it closes once the pointer goes below half the
least width, and opens again once it reaches that width, from the left of the page too; the
browser keeps both, the width and whether it is closed.

```
show style | include :root              ! the tokens and their defaults
set css sheet accent ':root { --accent: oklch(0.6 0.2 250) }'
set css sheet dark ':root { color-scheme: dark }'
set css sheet wide ':root { --width: 64rem }'
no css dark                             ! back to the colors of the system
```

## MCP servers

KiSS calls the tools of any MCP server over Streamable HTTP, with the official TypeScript SDK. A
server speaking the 2026 protocol is talked to in it, an older one through the `initialize`
handshake.

```
set mcp url sandbox https://example.com/mcp
set mcp key sandbox <token>
show tools                               ! every tool, by who serves it
set tools use str_replace off            ! the model never sees it
set tools preview bash_tool description  ! what a folded call shows
no tools bash_tool                       ! every setting of that tool, gone
set tools rounds 25                      ! the tool rounds a turn takes at most
set mcp timeout sandbox 300              ! the seconds a call may take
```

The key goes as `Authorization: Bearer <token>`. A browser cannot set that header on a WebSocket,
so WebSocket and stdio servers stay out of reach. A server must allow the origin of the page
(CORS). A call runs until it answers, within the timeout of its server, or until the turn is
stopped; connecting and listing the tools take five seconds at most.

## Models

Every model holds its own request parameters, sent under their OpenAI names and only when set;
the model is named as `chat model` takes it, `endpoint/model`.

```
show models                              ! every model, by endpoint, with its settings
set chat model prod/qwen3:8b
set models temperature prod/qwen3:8b 0.6
set models reasoning_effort prod/qwen3:8b high
no models prod/qwen3:8b                  ! every setting of that model, gone
set endpoints timeout prod 30            ! the seconds the endpoint has to start answering
```

The parameters are `temperature`, `top_p`, `top_k`, `min_p`, `max_tokens`, `presence_penalty`,
`frequency_penalty`, `seed` and `reasoning_effort`, the last one as the template of the model
reads it. A reply streams until it ends or the turn is stopped.

## Build

```
./build.sh
```

It formats, type checks, runs the laws, then builds.

The whole app is one file: `dist/index.html`. Serve it with `llama-server --path dist` or any
static server.

## Site defaults

An optional `kiss.conf` next to `index.html` gives a site its defaults, for instance the LLM a
fresh page talks to. It holds `set` lines only, read at every page load, below every save of the
user. Put it in `public/kiss.conf` to have the build copy it into `dist/`; that file is ignored by
git. Anyone who can load the page can read it, so a key set there is public.

```
set endpoints url prod https://example.com/v1
```
