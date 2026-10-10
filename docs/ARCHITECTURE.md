# KiSS architecture

The reference: how the page is built, the CLI it runs on, and the laws its tests hold.

## The page

The whole app is one file, `dist/index.html`, built by Vite with every script, style and font
inlined. It talks to an OpenAI compatible endpoint and to MCP servers straight from the browser;
there is no backend of its own.

```
src/engine/      the CLI: parse, schema, values, registry, run, archive
src/commands/    one file per command, registered by existing
src/modules/     one file per module: its keys, rules, checks, how it applies to the page
src/tools/       the tools of KiSS itself: config, the CLI as the model calls it
src/lib/         the page side: agent, api, mcp, state, conversation, db
src/components/  thread, message, round, ask, dialog, composer, sidebar
src/markdown/    the remark and rehype stack and its incremental renderer
tests/           the laws, under vitest and happy-dom
```

A file in `commands/`, `modules/` or `tools/` registers itself by existing. A command imports
`lib/types.ts` and `lib/config.ts` only and reaches everything else through its `Context`; a module
or a tool is where the configuration meets the page, it imports what it drives in `lib/`. A plugin
declared wrong or twice stops the page at load, never later.

## The engine

### Values

A value resolves through layers, the first that holds it winning: what the session sets, then
`kiss.conf`, then the default of the item, then the default of the key. A save keeps the session
layer only. A key is two fixed words, a module then a key; a key of a collection takes the item name
next, and the value always comes last. Its kind is `string`, `number`, `enum`, `url` or `secret`,
checked by the schema before anything runs.

### A batch

One batch runs at a time, whoever sends it: the user or the model.

1. Every line compiles before the first one runs: the command resolves, its rights and whether it
   runs alone are checked, its arguments parse against the schema, its filters compile.
2. The lines run in order on a draft: a copy of the running configuration and of the title of the
   conversation the batch was sent in.
3. The rules of every module hold on the draft.
4. For the model, the firewall reads the resolved change and asks the user when it must.
5. The draft replaces the running configuration and the title, the modules whose keys changed
   apply to the page, and the batch answers with its output, then its change, `-` then `+`.

A failure at any step applies nothing. A batch of a stopped turn neither starts, nor runs another
line, nor replaces anything.

### Words

Each word may be any prefix that names one word only, the way Cisco IOS reads a line; an exact word
always wins. Right after `set`, `no` or `show`, the words of longer commands and the names of the
modules compete, so `show d` is ambiguous between `diff` and `display`. A module named as a word of
a longer command could never be told from it, so the registry refuses it.

### The firewall

The model changes the page itself, so the engine guards the effect of its batches, never their
words. Between the rules and the commit, the resolved change of a batch of the model is read key by
key:

- a key declared `change` is guarded on any change: the url or the headers of an endpoint or an
  MCP server, `chat model`, `chat system`, and `privilege level` itself;
- a key declared `opening` is guarded on a change toward a more open value of its enum only:
  `tools use` goes `off`, `consent`, `on`, so closing never asks.

A guarded change goes as far as `privilege level <module>`: `deny` stops the batch, `ask` shows the
lines to the user, `allow` lets it. Since `set`, `no`, `reset` and `load` all end as a change of
resolved values, they are guarded alike. The answer takes once, always or refuse; always gives the
modules asked the `allow` privilege, in the same batch.

No privilege rules `privilege`: the registry refuses `privilege level privilege`, so a change of a
privilege by the model stays at the `ask` of its default, every time, and always gives it nothing.
The question says what always gives, and offers no always when it gives nothing.

The page loads its images, fonts, media and styles from itself only, a Content-Security-Policy in
`index.html` saying so; requests go anywhere, to the endpoints and the MCP servers the
configuration names. So neither an image in a reply nor a `url()` in a `css` sheet ever carries
anything out of the page.

A `set` line that leaves out the value of a secret asks the user for it in a masked field; the value
goes to the draft and never enters the conversation, and the output says
`! the user typed mcp key sandbox`. A line that writes the value of a secret is kept with it
removed, `set mcp key sandbox <removed>`, the way Cisco IOS strips a secret from what it hands out:
the brackets tell it is no value, and a `set` refuses it as one. A line that asks is kept as
written, so the history tells who gave the secret. A secret replaced reads as one line,
`! mcp key sandbox changed`.

## The CLI

The user types it after a `/`, and the model sends it through its tool.

```
batch   = line { newline line }              a line opening with ! is a comment
line    = command { "|" filter }
command = "set" key [ value ]                a secret left out is asked
        | "no" key | "no" module item
        | "show" module [ word [ item ] ] | "show" module item
        | "show" ( "running" | "style" | "saves" | "version" | "title" )
        | "show conversations"
        | "show diff" save save              the word session names the running one
        | "load" save | "reset"
        | "title" value                      the title of this conversation
        | "export" [ "all" | "running" | id ] alone on its line
        | "import"                           alone on its line
        | "delete" ( "all" | id )            alone, once the user confirms it
        | "save" save | "no save" save        user only, each alone on its line
key     = module word [ item ]               an item names one of a collection
save    = name                               a save of the same name gives way
value   = bare | "json string" | 'literal'
filter  = ( "include" | "exclude" | "begin" | "section" ) pattern | "count"
```

One write verb per store, one read verb for all: `set` and `no` write the configuration, `save`
and `no save` the archive, `title` the conversation, `import` the conversations, and `show` reads
every one of them.

An output by group indents each body one blank under its header, IOS style: `section` keeps a line
at the margin with the lines indented under it when that line matches, and an indented line reads
back as set like any other, so a whole output pastes back as it is.

| Module      | Keys                                                                             |
| ----------- | -------------------------------------------------------------------------------- |
| `chat`      | `model`, `system`, both guarded                                                  |
| `css`       | `sheet <name>`                                                                   |
| `display`   | `thinking`, `tools`, `render <thinking or reply>`                                |
| `endpoints` | `url <name>` and `headers <name>` guarded, `key <name>` secret, `timeout <name>` |
| `mcp`       | `url <name>` and `headers <name>` guarded, `key <name>` secret, `timeout <name>` |
| `models`    | `temperature` to `reasoning_effort <endpoint/model>`                             |
| `privilege` | `level <module>` guarded, every change asking                                    |
| `tools`     | `rounds`, `use <tool>` guarded when opening, `preview <tool>`                    |

### Laws

Each one is held by a test in `tests/`, and each guarantee checked by mutation.

- A command, module, key or filter word may be any prefix that names one word only; right after
  `set`, `no` or `show`, command and module words compete, `show d` being ambiguous between
  `diff` and `display`; an exact word always wins. A collection is named in the plural,
  `endpoints`, `tools`, `saves`, `models`, but an acronym, `css`, `mcp`: its singular, a prefix
  of it, names it too.
- A batch compiles all its lines, then runs them on a copy of the running configuration that
  replaces it only when every line succeeds, and answers with what changed.
- One batch runs at a time, whoever sends it. Once a turn is stopped, a batch of the model
  neither starts, nor runs another line, nor replaces anything.
- The line under a turn and what the turn spent measure the system alone: the time the user takes
  on a question leaves every clock of the turn that asks, a round counts its tokens once, by the
  count of the endpoint when it gives one, and the rate starts from the second token.
- A turn tries each MCP server once: a server that fails sits out its other rounds, and the next
  turn tries it again; a server connects as soon as the configuration names it.
- Each conversation answers on its own, beside the others: its turn, its Stop and its questions
  are its own, and deleting it stops its turn alone. Questions wait their turn, each answered to
  the turn that asked it.
- A value resolves from what the session sets, over `kiss.conf`, over the default of its key;
  `reset` drops what the session sets, `load` puts back the save it names. `kiss.conf` applies
  whole or not at all, like a batch.
- A line kept in the conversation never holds a secret, not even a line that does not read.
- The word after a collection names a key when it names one, else an item, for `no` and `show`
  alike.
- On an item holding a secret, the model sets the secret only: a secret goes to a URL the user
  or the site chose, never to one the model chose.
- The model changes a guarded key only as far as the privilege of its module goes, whatever
  command spells the change: `deny` refuses, `ask` asks the user, `allow` lets it; closing never
  asks, and a change of `privilege` asks every time.
- A question offers always only when it gives something, and says what.
- The thinking and the reply render as Markdown unless set `plain`; `render` takes these two
  blocks only.
- No reply and no sheet makes the page load from elsewhere: images, fonts, media and styles come
  from the page itself, checked in Chromium.
- A tool in `consent` asks the user before each call: once, always, which turns it on, or
  refuse. The tools of KiSS are on.
- A secret a `set` line leaves out is asked of the user, and never enters the conversation; the
  line that asks stays as written, a value written is kept as `<removed>`, which no `set` takes.
- No css sheet applies while a question stands, whatever it sets meanwhile: the card the user
  answers reads in the style of the page alone.
- `title` renames the conversation the batch was sent in, with the batch; a title is no
  configuration: no save keeps it, no `load` moves it.
- A conversation takes its title from its first message; one the CLI opened has none until then,
  and reads `CLI` where a title would: its title is a name, never a line typed.
- `delete` deletes nothing before the user confirms it, whoever asks, then every conversation it
  names in one write; `delete all` keeps the pinned ones, which go only when named.
- The latest save is the configuration the next page load starts with, and the archive changes
  only once the browser stores it.
- A model list answers within its timeout, its body included, so no endpoint holds the queue;
  every endpoint lists at once, one that fails beside the others.
- A save warns when no model answers the chat: `chat model` empty with more than one endpoint,
  or naming a model its endpoint does not serve.
- The sheets of `css` apply by name over every style of the page, whatever their selectors.
- A listing from many sources goes by group, `! <group>` over its lines, `! <group> <error>`
  alone when the source fails: `show models` by endpoint, `show tools` by who serves it.
- Every round of a turn reads the configuration as it stands: what a call changes holds from the
  next round on.
- A turn keeps, on the page and in the browser, what settled: what streamed and the calls that
  ended before a stop, never one that did not.
- A message copied gives its source: the text typed, the answer without its thinking or calls,
  the output of a command.
- An edit opens a branch beside the message it edits, the branch edited kept; the thread and the
  history the model reads are the path up from the leaf; a version comes back as it was last
  written in.
- An answer again enters beside the answers of its message, from the very prefix, every branch
  kept.
- A message enters with the moment it entered, and reads how long ago that was in the language
  of the browser, past a day in days of the calendar, so yesterday is the day the sidebar names so.
- A command closed leaves the tree, what followed it following the entry before it, the versions
  and the history the model reads as they were; a conversation it leaves empty goes with it.
- A conversation file reads back to every conversation as it settled, ids and branches kept, or
  imports nothing and says where it goes wrong; an import adds only the conversations whose id is
  new, and neither `export` nor `import` changes a conversation.
- Every MCP server serves its tools under their own names, after those of KiSS; a tool turned off
  is never seen by the model; a server that does not answer serves nothing, and a name already
  served stays with the first: the model is told, the save warns of it, nothing ever stops.

### Messages

Every message reads alike for a person and for a model: one line, in lower case, with no final
period. What was typed stands in double quotes, what KiSS names stands bare. An error starts with
`%`, a note with `!`; in a batch an error names its line, and the batch ends on
`% nothing applied`. Each kind of message has one form:

```
% unknown command "sow", did you mean show
% ambiguous word "d": diff display
% nothing goes after save a: b
% "2" is above 1
% nobody is here to pick a file
% the user refused the change
! no line matches
```

A fix follows an arrow: `no endpoint yet -> /set endpoints url <name> <url>`.

## Consent

The question shows in the conversation, in a card marked by the accent: the change the model asks
for, or the call with its real arguments, with Once, Always and Refuse, and what Always gives,
`Always allows chat` or `Always turns echo on`; a change of privilege alone takes Once and Refuse
only. A secret takes a masked field with OK and Cancel, an export Save or Cancel, an import Choose
file or Cancel, a delete OK or Cancel; Enter and Escape answer too, never alone. One question shows
at a time, in the order they come, whatever the conversation open: one from another conversation
names it, as it holds every batch until answered. The Stop of a conversation answers no to its own
questions. While a question stands the css sheets hold off, so no sheet restyles, hides or covers
the card. Monospace is for what a machine wrote: the calls of tools and their outputs, code, and a
block rendered plain; the cards, the commands and the errors read in the font of the page.

```
show privilege                           ! every guarded module and how far the model goes
set privilege level chat allow           ! the model picks its model and prompt freely
set privilege level endpoints deny       ! it never changes an endpoint
set tools use bash_tool consent          ! each call asks, until always
set mcp key sandbox                      ! no value: a masked field asks for it
```

## A turn

A turn picks the endpoint and the model `chat model` names, `endpoint/model`; left empty, it takes
the one model of the one endpoint, any other choice being the user's, since a router loads whatever
model it is asked for. The tools the model sees are those of KiSS, then those of every MCP server,
those turned off left out; a server that fails, or a name served twice, becomes a system note to the
model, which tells the user, the note kept until the turn ends, so what the model did about a server
stays explained once the server is back. Each round streams its thinking, its text and its calls,
the arguments of a call read as the stream writes them, their JSON closed where it stops; a round
that calls nothing ends the turn, at most `tools rounds` of them. Every round reads the
configuration as it stands, so a tool a call turns on is offered from the next round on, as is a
model, a system prompt or a parameter a call changes. A tool in `consent` asks before its call, and
a turn stopped keeps what settled.

Every endpoint and every MCP server sends its key as a bearer token, and the headers it is given
besides, `Name: value` pairs split by `;`, to it alone: an API that answers a page only once it
names itself, such as the Claude API, takes `anthropic-dangerous-direct-browser-access: true`.

Every model holds its own request parameters, sent under their OpenAI names and only when set.

```
show models                              ! every model, by endpoint, with its settings
set chat model prod/qwen3:8b
set models temperature prod/qwen3:8b 0.6
set models reasoning_effort prod/qwen3:8b high
no models prod/qwen3:8b                  ! every setting of that model, gone
set endpoints timeout prod 300           ! the seconds to start answering, 120 by default
```

The parameters are `temperature`, `top_p`, `top_k`, `min_p`, `max_tokens`, `presence_penalty`,
`frequency_penalty`, `seed` and `reasoning_effort`, the last one as the template of the model
reads it. A reply streams until it ends or the turn is stopped. Several conversations answer at
once, each turn in its own; a server running one model at a time loads them in turn.

One line under the turn says what the system does now, each of its parts capital, redrawn at every
frame of the screen, in milliseconds: `Preparing - 12 ms` while a round reads its model and gathers
its tools, `Waiting for pod - 842 ms` until the first chunk, thinking or writing with the tokens and
their rate, `Writing - 3,518 ms - 177 tokens - 51.3 t/s`, calling a tool while its call streams,
`Running config - 47 ms` while it runs, after `Round 2 - ` from the second round on. It hides while
a question of the turn stands, as the time the user takes is none of the system. A request asks the
endpoint to count its tokens, `stream_options` of OpenAI, one per chunk counting until it does. Once
the turn ends, what it spent stays with its answer, beside its time:
`1,204 tokens - 51.3 t/s - 23,512 ms`, the rate over the generation alone, the last figure the whole
time of the system.

## MCP servers

KiSS calls the tools of any MCP server over Streamable HTTP, with the official TypeScript SDK. A
server speaking the 2026 protocol is talked to in it, an older one through the `initialize`
handshake. One client per server lives while its url, key, headers and timeout stay, with the tool
list it served once connected, so the tools the model sees stay the same from turn to turn; a
request that fails drops the client, and the next use connects again. A server connects as soon as
the configuration names it, at load or on a change, so the first round that needs its tools finds
them listed; a round waits only for a server still connecting, and its line names it,
`Preparing mcp sandbox - 812 ms`. A server that fails sits out the rest of the turn, its error told
to the model every round, and the next turn tries it again: a server that never answers costs one
wait a turn, the tools of a turn the same from round to round.

```
set mcp url sandbox https://example.com/mcp
set mcp key sandbox                      ! a masked field asks for the token
show tools                               ! every tool, by who serves it
show tools | section sandbox             ! one server, its tools indented under it
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

## Conversations

A conversation is a tree, kept flat: every entry holds its id, the entry it follows and the moment
it entered, and the conversation holds its leaf, the entry the thread ends on. The thread is the
path up from the leaf, and so is the history the model reads, so the server reuses its cache along
it.

Under every message, shown while it is hovered, always under the last message of the thread and
where nothing hovers, how long ago it entered, its first letter capital, its moment in full under
the pointer, in the language of the browser, and its copy button on the outer edge, the versions and
the time on the inner side: under a message of the user, retry, edit and copy; under an answer, copy
and retry; under a command, copy and the time, no versions. A command reads like a block of code, a
head naming the CLI, `CLI`, with its close button in the corner. Retry, from a message of the user
or from an answer to it, enters a new answer beside those it had, from the very prefix they were
given. The model never reads a command, so closing one clears the thread and leaves its history
whole. What stops a turn shows under it in a card marked by the danger color, like a question to the
user. An edit enters the new text as a version beside the message it edits, and the model answers it
from the very prefix the edited one had; the branch edited stays whole. Arrows under a message with
versions go from one to the next, each coming back as it was last written in. The configuration
belongs to no branch: a command of one branch stays applied when another shows.

Files go through the CLI, so the model handles them as well as the user. `show conversations` lists
them by id, the one the batch was sent in marked. `export` offers a file of this conversation, of
another by a prefix of its id, of all of them, or of the running configuration as `kiss.conf`, the
`set` lines of `show running`, secrets left out, in a card the user saves from: a browser saves a
file on a click only, so the model never puts one on the disk by itself. `import` asks for a file in
a card the user picks from and reads it by what it holds: a JSON file adds its conversations whose
id is new, the others skipped and told; any other file runs its lines as more lines of the batch,
whole or not at all, with the rights of whoever asked, the firewall included, the way lines pasted
after a `/` run. Neither changes a conversation, the one shown included: an export reads the
conversations as they settled, without the call that exports them, and an import only adds. To keep
an export out of the context, edit the message that asked for it.

```
show conversations
export                                   ! this conversation
export 3f2a                              ! another, by a prefix of its id
export running                           ! the configuration, as kiss.conf
export all
import
delete 3f2a                              ! once the user confirms it
delete all                               ! all but the pinned ones
```

One file format holds one conversation or many, marked `kiss`, every branch, every id and every pin
in it, read whole or refused with where it goes wrong: every conversation once, every entry id once,
every parent before its child, a leaf that ends a branch. The sidebar lists New chat first, lit
while no conversation is open and staying at the top while the list scrolls, then the conversations,
the last answered first: the pinned ones under Pinned, the others under the day of their last
answer, today, yesterday, then their date, in the language of the browser. Beside each, a pin pins
it or puts it back under its day, and a cross deletes it once the user confirms, Enter or OK, Escape
or a click elsewhere cancelling; while the model answers in it, a dot of the accent stands at its
right, giving way to the pin and the cross on hover. Every item runs from the left edge of the page
to the scrollbar, and a title too long fades out rather than losing letters to an ellipsis. A
conversation dates from its last answer: a command, a version shown, a pin or a title dates nothing.

The browser keeps the conversations in IndexedDB, as they settled, the saves in `localStorage`
under `kiss.saves`, and the width of the sidebar under `kiss.sidebar`.

## Rendering

The thinking and the reply render as Markdown, each plain on demand, `set display render reply
plain`: a plain block reads as written, in monospace. The thinking keeps its small size and the font
of the page, its headings at its size. Markdown renders with remark and rehype: GitHub flavored
Markdown, LaTeX through KaTeX, code highlighted in every language lowlight knows, under a head
naming it with a copy button. Raw HTML shows as the text it is, a link to anything but a web page or
a mail address keeps its text only, and a wide table scrolls in its own box. While a reply streams,
every block but the last renders once and is kept, and the page renders at most once per frame.

## Style

The page style is plain CSS, its tokens on `:root`, one theme and no other: three colors, `--bg`,
`--fg` and `--accent`, every other color derived from them, shades of the background, shares of the
text, the accent drawn toward the text, and hues at one lightness that follows the text, with the
most chroma the screen shows at any lightness without bending the hue, so a sheet setting the three
sets a whole theme, dark or light alike, and a sheet setting a derived color sets it alone;
`--radius`, `--font`, `--mono`, `--size-primary` for the chat and `--size-secondary` for everything
around it, every scrollbar thin, its thumb a line, the only two sizes of text but `--size-title`,
the title of the page, `--size-icon`, every icon, the primary size, `--width`, `--bubble-width`,
`--sidebar-width` within `--sidebar-min` and `--sidebar-max`. `show style` lists it, one rule per
line, a rule of a component with its scoping class, the name of the component, `svelte-composer` for
`Composer.svelte`, so a selector read there aims at that component alone; named sheets restyle
anything over it. The sidebar follows its edge for the whole drag, within those bounds, it closes
once the pointer goes below half the least width, and opens again once it reaches that width, from
the left of the page too; the browser keeps both, the width and whether it is closed.

```
show style | include :root              ! the tokens and their defaults
set css sheet accent ':root { --accent: oklch(0.6 0.2 250) }'
set css sheet theme ':root { --bg: #faf9f5; --fg: #141413 }'
set css sheet wide ':root { --width: 64rem }'
set css sheet input 'form.svelte-composer { max-width: 64rem }'  ! the composer alone
no css theme                            ! back to the colors of the page
```

## Site defaults

An optional `kiss.conf` next to `index.html` gives a site its defaults, for instance the LLM a
fresh page talks to. It holds `set` lines only, read at every page load, below every save of the
user. Put it in `public/kiss.conf` to have the build copy it into `dist/`; that file is ignored by
git. Anyone who can load the page can read it, so a key set there is public.

```
set endpoints url prod https://example.com/v1
```

## Build

```
./build.sh
```

It installs, formats, type checks, runs the laws, then builds `dist/index.html`.
