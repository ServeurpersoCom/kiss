# KiSS page

The reference of the frontend: how the page is built, the CLI it runs on, and the laws its tests
hold.

## Commands

```
commands
  set <module> <key> [name] <value>
  no <module> <key> [name]
  no <module> <name>
  show <module> [key] [name]
  show <module> <name>
  show running-config [all]
  show startup-config
  show saves
  show diff <config> <config>
  show css
  show title
  show conversations
  show version
  copy <config> <config>
  write [memory]
  erase <id>|conversations|<config>
  export [<id>|conversations|<config>]
  import
  title <title>
  attach
  detach
  <command> | begin|count|exclude|include <pattern>

  <config>                running-config, startup-config or a save by its name
  <model>                 endpoint/model, as show models lists it

modules
  chat
    model <model>                   the one model of the one endpoint
    system-prompt <text>            none
  client
    hostname <name>                 four letters drawn once
  display
    thinking closed|open|inference  closed
    tool-calls closed|open|inference
                                    closed
    render thinking|reply markdown|plain
                                    markdown
    preview-argument <argument>     description, the argument every folded call shows
  endpoints
    url <name> <url>                guarded
    protocol <name> chat|messages|responses
                                    chat
    key <name> <secret>
    auth-header <name> <Header[: Scheme]>
                                    the header of the protocol
    headers <name> <Name: value; ...>
    timeout <name> <seconds>        120
    max-retries <name> <count>      1800, a reply asked again on 408, 429, 5xx, 529
    retry-pause <name> <seconds>    2, between two
    proxy <name> <server>           guarded, the KiSS server whose route holds it
  mcp
    url <name> <url>                guarded
    key <name> <secret>
    auth-header <name> <Header[: Scheme]>
                                    Authorization: Bearer
    headers <name> <Name: value; ...>
    timeout <name> <seconds>        310
    proxy <name> <server>           guarded, the KiSS server whose route holds it
  models                            show models lists them, by endpoint
    temperature <model> <number>
    top_p <model> <0..1>
    top_k <model> <integer>
    min_p <model> <0..1>
    max_tokens <model> <integer>
    presence_penalty <model> <-2..2>
    frequency_penalty <model> <-2..2>
    seed <model> <integer>
    reasoning_effort <model> <word>
  style
    <token> <value>                 show style lists them; bg, fg, accent set a theme
    sheet <name> <css>
  tools                             show tools lists them, by who serves them
    max-rounds <count>              1000
    use <tool> off|consent|on       consent, on for the tools of KiSS, guarded
    preview-argument <tool> <argument>
                                    display preview-argument, the argument of this tool over it

tools
  config                            lines
```

## The page

The whole app is one file, `webui/dist/index.html`, built by Vite with every script, style and font
inlined. It talks to an OpenAI compatible endpoint and to MCP servers straight from the browser; the
KiSS server of `server/`, described in `BACKEND.md`, is one MCP server among others.

```
webui/client.conf       the factory configuration the page bundles
webui/src/main.ts       the start: site configuration, startup-config, conversations, page
webui/src/prompts.json  what a model reads of the page: its tools, its notes
webui/src/engine/       the CLI: text, parse, filter, near, schema, values, registry, archive, run,
                        complete, and the keychain with its seal
webui/src/commands/     one file per command, registered by existing
webui/src/modules/      one file per module: its keys, rules, checks, how it applies to the page
webui/src/tools/        one file per tool, registered by existing: config, the CLI as the model
                        calls it
webui/src/protocols/    one file per protocol an endpoint speaks: chat, messages, responses
webui/src/lib/          the page side: agent, api, mcp, form, state, sync, conversation, db
webui/src/rendering/    how the page shows what it holds: App.svelte, components/, thread,
                        message, round, ask, dialog, composer, sidebar, markdown/, the remark
                        and rehype stack and its incremental renderer, tokens.css, the tokens
                        of the page, read as the keys of style, and modules/ and commands/ of
                        the configuration that drive it: display, style, show css
webui/tests/            the laws, under vitest and happy-dom
```

A file in `commands/`, `modules/`, `tools/` or `protocols/` registers itself by existing, as one in
`rendering/commands/` or `rendering/modules/` does, in the same configuration. A command
imports the engine, `lib/types.ts` and `lib/config.ts` only and reaches everything else through its
`Context`; a module or a tool is where the configuration meets the page, it imports what it drives in
`lib/`. A plugin declared wrong or twice stops the page at load, never later.

## The engine

### Values

A value resolves through layers, the first that holds it winning, as on a device of Cisco IOS: what
the session sets, then `client.conf` of the site, then the factory configuration `webui/client.conf`
the page bundles, then the reset value the key declares. The factory configuration holds only what
differs from a reset value. A save keeps the session layer only, and so does the startup-config. A
key is two fixed words, a module then a key; a key of a collection takes the item name next, and the
value always comes last. Its kind is `string`, `number`, `enum`, `url`, `secret` or `css`, a value
of the CSS property it names, checked by the schema before anything runs.

### A batch

One batch runs at a time, whoever sends it: the user or the model.

1. Every line compiles before the first one runs: the command resolves, its rights and whether it
   runs alone are checked, its arguments parse against the schema, its filter compiles.
2. The lines run in order on a draft: a copy of the running configuration and of the title of the
   conversation the batch was sent in.
3. The rules of every module hold on the draft.
4. For the model, the firewall reads the resolved change and asks the user when it must.
5. The draft replaces the running configuration and the title, the modules whose keys changed
   apply to the page, and the batch answers with its output, then its change, `-` then `+`: every
   register the session layer moves, read through the layers before and after, a register of an
   item existing while a layer names the item or its module lists it, so a line shown holds before
   or after the batch.

A failure at any step applies nothing. A batch of a stopped turn neither starts, nor runs another
line, nor replaces anything.

### Words

A word that names something already there may be any prefix that names it alone, the way Cisco IOS
reads a line; an exact word always wins, and the configuration keeps the whole word. That holds for
a command, a module, a key, a filter, the value of an enum (`set display tool-calls op` sets
`open`), `all` and `memory`, the item of `no` and `show`, a configuration (`copy run start`), and
what `erase` and `export` name. A word that may be new is written whole: the item of a `set`, which
may create it, a save a copy creates, and every value but the one of an enum, `chat model` included.

Right after `set`, `no` or `show`, the words of longer commands and the names of the modules
compete, so `show d` is ambiguous between `diff` and `display`. A module named as a word of a longer
command could never be told from it, so the registry refuses it.

Every word completes, on Tab or by a click on the words under the composer: the words above, the
items set and those the modules know (the models of every endpoint, the tools of every server),
listed again each time a line opens on the CLI, the filters after a pipe, the title of the
conversation, and for a value its current one, a secret aside, and the items of the module it
names: `set chat model hf/` offers every model of `hf`.

### The firewall

The model changes the page itself, so the engine guards the effect of its batches, never their
words. Between the rules and the commit, the resolved change of a batch of the model is read key by
key:

- a guarded url, of an endpoint or an MCP server, asks when it takes a value: the page would
  send there the conversation, or the arguments of a call;
- a guarded enum asks when it moves toward its more open values: `tools use` goes `off`,
  `consent`, `on`, so closing never asks.

The question shows the lines and takes once or refuse, every time: nothing turns it off, so no
answer given once leaves a way out open. Since `set`, `no`, `erase` and `copy` all end as a change
of resolved values, they are guarded alike. Everything else, the model of the chat, its prompt, the
style, the headers, the sampling, the model changes freely, the change shown in its output.

The secrets of an item go with it to a url the user lets the model give it: the question names them,
`! endpoints key u goes with it`, so a key is never typed again for a url that changes. A secret and
a url born in one batch go together, in either order, the url asking the user, who refuses both at
once.

What loses data asks too, once the user confirms it, whoever asks for an erase and the model for a
copy to the startup-config or a save: nothing is left that the user alone may do.

The page loads its images, fonts, media and styles from itself only, a Content-Security-Policy in
`index.html` saying so; requests go anywhere, to the endpoints and the MCP servers the
configuration names. So neither an image in a reply nor a `url()` in a style sheet ever carries
anything out of the page.

A `set` line that leaves out the value of a secret asks the user for it in a masked field; the value
goes to the draft and never enters the conversation, and the output says
`! the user typed mcp key sandbox`. A line that writes the value of a secret is kept with it
removed, `set mcp key sandbox <removed>`, the way Cisco IOS strips a secret from what it hands out:
the brackets tell it is no value, and a `set` refuses it as one. A line that asks is kept as
written, so the history tells who gave the secret. A secret replaced reads as one line,
`! mcp key sandbox changed`.

As the page opens, an MCP server that takes no request without its key, a KiSS server off loopback,
asks the user for it the same way: the page runs `set mcp key <name>`, again while the key is
wrong, until the user gives none.

## The CLI

The user types it after a `/`, and the model sends it through its tool.

```
batch   = line { newline line }              a line opening with ! is a comment
line    = command [ "|" filter ]
command = "set" key [ value ]                a secret left out is asked
        | "no" key [ value ] | "no" module item  a value is left as IOS does
        | "show" module [ word [ item ] ] | "show" module item
        | "show running-config" [ "all" ]    the overlays, or the state of every key
        | "show" ( "startup-config" | "css" | "version" | "title" )
        | "show saves" [ save ]              every save, or one as the lines that set it
        | "show conversations"
        | "show diff" config config
        | "copy" config config               alone; the model writes another config once confirmed
        | "write" [ "memory" ]               alone, IOS's copy running-config startup-config
        | "title" value                      the title of this conversation
        | "export" [ "conversations" | config | id ] alone on its line
        | "import"                           alone on its line
        | "erase" ( "conversations" | config | id ) alone, once the user confirms it
key     = module word [ item ]               an item names one of a collection
config  = "running-config" | "startup-config" | save   any prefix: run, start, a save
save    = name                               whole when a copy makes it, one of that name giving way
value   = bare | "json string" | 'literal'
filter  = ( "include" | "exclude" | "begin" | "count" ) pattern
pattern = the rest of the line as written    a regular expression minding case, | included
```

One verb per gesture: `set` and `no` write a line of the configuration, `copy` a configuration
whole, `write memory` the running-config to the startup-config as IOS saves, `erase` removes a
configuration or conversations whole, `title` writes the conversation, `import` adds
conversations, and `show` reads every one of them.

Every line stands alone, a key named whole, so the CLI has no mode to enter or leave and no line
depends on another. An output by group writes each header as a note before its lines, so a whole
output pastes back as it is.

Every line an output writes is one of four: a command, which pastes back as it is, a change after
`-` or `+`, a note after `!`, which a paste skips, what a command did, or an error after `%`. What a
listing reads goes in notes: the saves with their dates, the conversations by id, the one the batch
was sent in marked `*`, the version, the words that may follow a line that stops short, and `count`
in the words of IOS, `! number of lines which match regexp = 12`; `show css` alone writes the rules
of the page as they are, under its notes by component.

Two rules hold every word KiSS writes. The CLI is the same with itself: its words as they are, in
lower case, keys and values verbatim. Everything else the page tells the user or the model opens
with a capital, the result of a call KiSS writes, the error of a turn, the status line, unless it
opens on a URL, written as it is; what a server answers goes as it came.

| Module      | Keys                                                                                                                                                                                       |
| ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `chat`      | `model`, `system-prompt`                                                                                                                                                                   |
| `client`    | `hostname`                                                                                                                                                                                 |
| `display`   | `thinking`, `tool-calls`, `render <thinking or reply>`, `preview-argument`                                                                                                                 |
| `endpoints` | `url <name>` guarded, `protocol <name>`, `key <name>` secret, `auth-header <name>`, `headers <name>`, `timeout <name>`, `max-retries <name>`, `retry-pause <name>`, `proxy <name>` guarded |
| `mcp`       | `url <name>` guarded, `key <name>` secret, `auth-header <name>`, `headers <name>`, `timeout <name>`, `proxy <name>` guarded                                                                |
| `models`    | `temperature` to `reasoning_effort <endpoint/model>`                                                                                                                                       |
| `style`     | `bg` to `sidebar-max`, the tokens of the page, `sheet <name>`                                                                                                                              |
| `tools`     | `max-rounds`, `use <tool>` guarded when opening, `preview-argument <tool>`                                                                                                                 |

### Laws

Each one is held by a test in `tests/`, and each guarantee checked by mutation.

- A line takes one filter, after its first pipe outside quotes, as IOS does: the pattern is the rest
  of the line as written, so `| include ^set (tools|style)` reads whole; an output comes whole, as
  long as it is, the filter narrowing it.
- A word naming something already there, a command, module, key, filter, enum value, item of `no` or
  `show`, configuration or conversation, may be any prefix that names one only; a word that may be
  new, the item of a `set`, a save a copy creates, a value, is written whole; right after `set`,
  `no` or `show`, command and module words compete, `show d` being ambiguous between `diff` and
  `display`; an exact word always wins. A collection is named in the plural, `endpoints`, `tools`,
  `saves`, `models`, its singular, a prefix of it, naming it too; an acronym, `mcp`, or a whole,
  `display`, `style`, keeps its one name.
- Every word completes: a value with its current one, never the one of a secret, and with the items
  of the module it names, the models of every endpoint for `chat model`.
- A batch compiles all its lines, then runs them on a copy of the running configuration that
  replaces it only when every line succeeds, and answers with what changed; it always answers, `! no
  change` when it writes nothing and changes nothing, so a request never ends in silence.
- Nothing reaches the network before it passes the firewall: a batch writes a draft, and what lists
  models or tools reads the configuration as it applies, so a show after a set in the same batch
  reads what was there before.
- One batch runs at a time, whoever sends it. Once a turn is stopped, a batch of the model neither
  starts, nor runs another line, nor replaces anything.
- A request holds one system message, first, as every template takes it: the system prompt, then
  what the turn left out.
- The line under a turn and what the turn spent measure the system alone: the time the user takes on
  a question leaves every clock of the turn that asks, a round counts its tokens once, by the count
  of the endpoint when it gives one, and the rate starts from the second token.
- A turn tries each MCP server once: a server that fails sits out its other rounds, and the next
  turn tries it again; a server connects as soon as the configuration names it.
- Each conversation answers on its own, beside the others: its turn, its Stop and its questions are
  its own, and deleting it stops its turn alone. Questions wait their turn, each answered to the
  turn that asked it.
- A value resolves from what the session sets, over `client.conf`, over the factory configuration,
  over the reset value of its key; `erase running-config` drops what the session sets, a copy to
  `running-config` puts back the configuration it names.
- Every configuration shows as it runs, over `client.conf`, the keys no module declares anymore left
  out: `show`, `show diff` and `export` alike, while a save keeps what the session sets only, so a
  `client.conf` changed reaches every save. `show running-config` lists what the session and the
  site set over the factory, `show running-config all` every key of every module as `show <module>`
  lists it.
- A line kept in the conversation never holds a secret, not even a line that does not read.
- The keychain of the page is the one reader of the value of a key beside the engine that holds it:
  a module reads a secret `<removed>`, and the key goes out at one place, the header its
  `auth-header` names, `Header` or `Header: Scheme`, read as the request leaves. What leaves the
  memory of the page is sealed: the request to the model, a conversation stored or exported, every
  string in it and every name of a property read `<removed>` where a secret the page has known this
  session stood, a value typed in the form of a server and a key replaced since included. A string
  is sealed as decoded, so a key escaped or streamed in pieces is caught, and a conversation kept
  before is sealed as it leaves again. The screen shows what came. The headers of an item never name
  the header its `auth-header` writes.
- The word after a collection names a key when it names one, else an item, for `no` and `show`
  alike.
- The secrets of an item go with it to a url the model writes once the user lets it, the question
  naming them; a secret and a url born in one batch go together, in either order.
- The model opens a way out only once the user agrees, every time, whatever command spells the
  change: a url given a value, a tool opened; closing never asks.
- A question offers always only for a call, and says what it gives.
- The thinking and the reply render as Markdown unless set `plain`; `render` takes these two blocks
  only.
- No reply and no sheet makes the page load from elsewhere: images, fonts, media and styles come
  from the page itself, checked in Chromium.
- The images of a call go to the model with its result, every round, when the list of its endpoint
  says it sees them or says nothing, never when it says it does not.
- A tool in `consent` asks the user before each call: once, always, which turns it on, or refuse.
  The tools of KiSS are on. A call reaches its tool only while the tool is not off, as it stands
  when the call goes: a tool a call turns off is reached by no later call of the round.
- A secret a `set` line leaves out is asked of the user, and never enters the conversation; the line
  that asks stays as written, a value written is kept as `<removed>`, which no `set` takes.
- No style token or sheet applies while a question stands, whatever it sets meanwhile: the card the
  user answers reads in the style of the page alone.
- `title` renames the conversation the batch was sent in, with the batch; a title is no
  configuration: no save keeps it, no `copy` moves it.
- A conversation takes its title from its first message; one the CLI opened has none until then, and
  reads `CLI` where a title would: its title is a name, never a line typed.
- `erase` removes nothing before the user confirms it, whoever asks; a word naming a configuration
  names no conversation; every conversation it names goes in one write, `erase conversations`
  keeping the pinned ones, which go only when named. The model writes a configuration but the
  running-config once the user confirms it too.
- `client.conf`, then the startup-config, enter as IOS boots: a line the parser refuses stays out,
  then the rest goes block by block, an item with all its keys, a block a rule refuses staying out
  and tried again once others entered; the console tells each one, `startup-config: endpoints u:
  endpoints: endpoints u has the url of endpoints s`, and the page starts on the rest, so the
  running configuration always holds the rules. The factory configuration holds them too, or the
  page does not start.
- The page starts with the startup-config over `client.conf`: a copy to `startup-config` writes it
  and `erase startup-config` empties it, a save never touches it; the archive changes only once the
  browser stores it.
- Every line `show running-config` writes pastes back after `no`, whatever value follows its key,
  and removes only itself, unless a rule of its module says why it stays: a model still names the
  endpoint it would remove.
- A line names only an item its module may hold: one the configuration holds, or one its source
  serves, a model its endpoint lists, a tool KiSS or an MCP server serves, the source read from the
  draft for the user and from the configuration as it applies for the model; a source that fails
  refuses the line with its error. `chat model` names an endpoint of the configuration, the model
  left to the endpoint. A model names an endpoint, and holds no parameter its protocol never sends.
- A save or the startup-config drops what no key takes anymore, a key or an item of a key that takes
  known items only, `display render`, `tools use`, or a value its key no longer takes, at boot, in a
  copy and wherever it shows, and a copy says so; a line naming one fails.
- A model list answers within its timeout, its body included, so no endpoint holds the queue; every
  endpoint lists at once, one that fails beside the others.
- A copy from `running-config` warns when no model answers the chat: `chat model` empty with more
  than one endpoint, or naming a model its endpoint does not serve; an endpoint that fails warns
  once, by its name.
- The keys of `style` are the tokens of the page of a value of their own, the model free on them; a
  token derived from others is no key. The sheets of `style` apply by name over every style and
  every token of the page, whatever their selectors.
- The page reads every size of text floored, so no size hides what the thread shows.
- A listing from many sources goes by group, `! <group>` over its lines, `! <group> <error>` alone
  when the source fails: `show models` by endpoint, `show tools` by who serves it, `show css` by
  component; a listing from one source, `show display`, reads at the margin. A setting no source
  serves now goes last, under `! unavailable`, whatever keeps it away, a source down or a name gone,
  and stays until a `no` drops it.
- Every round of a turn reads the configuration as it stands: what a call changes holds from the
  next round on.
- A turn keeps, on the page and in the browser, what settled: what streamed, the calls that ended
  before a stop, and the one a stop or a page closed cut while it ran, with its arguments and as
  stopped, since the tool may have acted; a call never sent leaves nothing.
- A message copied gives its source: the text typed, the answer without its thinking or calls, the
  output of a command.
- An edit opens a branch beside the message it edits, the branch edited kept; the thread and the
  history the model reads are the path up from the leaf; a version comes back as it was last written
  in.
- An answer again enters beside the answers of its message, from the very prefix, every branch kept.
- A message enters with the moment it entered, and reads how long ago that was in the language of
  the browser, past a day in days of the calendar, so yesterday is the day the sidebar names so.
- A command closed leaves the tree, what followed it following the entry before it, the versions and
  the history the model reads as they were; a conversation it leaves empty goes with it.
- A conversation file reads back to every conversation as it settled, ids and branches kept, or
  imports nothing and says where it goes wrong; an import adds only the conversations whose id is
  new, and neither `export` nor `import` changes a conversation.
- Every MCP server serves its tools under their own names, after those of KiSS; a tool turned off is
  never seen by the model; a server that does not answer serves nothing, and a name already served
  stays with the first: the model is told, a copy warns of it, nothing ever stops.

### Messages

Every message reads alike for a person and for a model: one line, in lower case, with no final
period. What was typed stands in double quotes, what KiSS names stands bare. An error starts with
`%`, a note with `!`; in a batch an error names its line, and the batch ends on
`% nothing applied`. Each kind of message has one form:

```
% unknown command "sow", did you mean show
% ambiguous word "d": diff display
% nothing goes after copy running-config a: b
% "2" is above 1
% nobody is here to pick a file
% the user refused the change
! no line matches
```

A fix follows an arrow: `no endpoint yet -> /set endpoints url <name> <url>`.

## Consent

The question shows in the conversation, in a card marked by the accent: the change the model asks
for, with Once and Refuse, or the call with its real arguments, with Once, Always and Refuse, and
what Always gives, `Always turns echo on`. A secret takes a masked field with OK and Cancel, an
export Save or Cancel, an import Choose file or Cancel, an erase or a copy OK or Cancel; Enter and
Escape answer too, never alone. A question shows in the conversation that asks it, one at a time in
the order they come there, and the dot of that conversation in the sidebar blinks while it waits. A
batch that asks leaves the queue of the CLI while the user thinks, nothing applied, so the other
conversations and the commands of the user run meanwhile; it then runs again from its first line
with the answer, on the configuration as it stands then. What a question is about is figured once:
an export saves the very file it offered, an erase removes the conversations its question named and
none that came since. The Stop of a conversation answers no to its own questions. While a question
stands the style tokens and sheets hold off, so nothing restyles, hides or covers the card.
Monospace is for what a machine wrote: the calls of tools and their outputs, the commands of the CLI
and their outputs, the lines and the arguments a card shows, code, and a block rendered plain, so a
`-` and a `+` line up; the heads of the cards and of the commands, the answers and the error of a
turn read in the font of the page.

```
! each call asks, until always
set tools use bash_tool consent
! no value: a masked field asks for it
set mcp key sandbox
```

## A turn

A turn picks the endpoint and the model `chat model` names, `endpoint/model`; left empty, it takes
the one model of the one endpoint, any other choice being the user's, since a router loads whatever
model it is asked for. The tools the model sees are those of KiSS, then those of every MCP server,
those turned off left out; a server that fails, or a name served twice, becomes a system note to the
model, which tells the user, the note kept until the turn ends, so what the model did about a server
stays explained once the server is back. Each round streams its thinking, its text and its calls,
the arguments of a call read as the stream writes them, their JSON closed where it stops; a round
that calls nothing ends the turn, at most `tools max-rounds` of them, which the model may raise like
any free key, a cost shown in its change. A line of the stream that does not read stops the turn
naming the endpoint and the line. Every round reads the configuration as it stands, so a tool a call
turns on is offered from the next round on, as is a model, a system prompt or a parameter a call
changes. A tool in `consent` asks before its call, and a turn stopped keeps what settled.

Every endpoint speaks the protocol `endpoints protocol` names: the protocol builds the request from
the history, carries the key and reads the stream, and the rest of KiSS sees the same deltas
whatever it is. A round keeps what its protocol needs back and nothing else rebuilds, a signed
thinking block, an encrypted reasoning item, and sends it back to that protocol only. The images a
call answers with go to the model with its result, every round, in the form its protocol takes,
when the list of the endpoint says the model sees them or says nothing; a model that does not
reads the line its result keeps of each, `Image image/png, 4096 bytes`.

- `chat`, by default: the chat completions of OpenAI, the open standard of llama.cpp, vLLM and
  Hugging Face, the key as a bearer token unless `auth-header` says. A tool message holds text only,
  so the images of a round follow its results in one user message; `architecture.input_modalities`
  in the list, as llama-server and Hugging Face give it, says whether the model sees them.
- `messages`: the Messages API of Anthropic, the key in `x-api-key` unless `auth-header` says, with
  the headers a page needs, the betas the headers of the endpoint name joining its own, the thinking
  summarized and sent back as it came, the prompt cached up to its last block, every tool streaming
  its arguments, the images of a call in its result, unless `capabilities.image_input` says
  otherwise. A signed block is bound to the system, the tools and the messages before it: one the
  configuration changed since, a tool opened or a prompt edited, is dropped by the API and thought
  again, never refused. The list of the endpoint gives the most tokens the model writes and whether
  it thinks, `models max_tokens` over it; `min_p`, the penalties and `seed` never go.
- `responses`: the Responses API of OpenAI, stateless, the whole history sent every round and
  nothing stored on the server, the key as a bearer token unless `auth-header` says, the system
  prompt as its instructions, the reasoning streamed as its summary and sent back encrypted as it
  came, every tool as it is written, the images of a call in its output. `max_tokens` goes as
  `max_output_tokens`; `top_k`, `min_p`, the penalties and `seed` never go.

An endpoint is reached directly at its url, or through the route of its name on a KiSS server, as an
MCP server is: `set endpoints proxy hf monstre` reaches `hf` at `proxy/hf` beside the url of
`monstre`, an item of `mcp`, with the key, auth-header and headers of `monstre` beside the fixed
headers of the protocol; the route holds the url and the key it sends, so a key of a cloud API never
reaches the browser, and the url and key of `hf` here wait unused until `no endpoints proxy hf`
sends it direct again, one line either way. The batch that sets a proxy asks the server for its
routes, which it publishes as MCP resources: one without a route of that name, or without a key when
the item holds one, stops the batch with the fix, `set proxy key hf on mcp monstre`, so the model
never cuts itself off its own endpoint. The proxy names a KiSS server of the page that has a url and
no proxy itself, the refusal of any other listing those, and the model sets it only once the user
agrees, as a url.

```
set endpoints url claude https://api.anthropic.com/v1
set endpoints protocol claude messages
set chat model claude/claude-opus-5-5
set endpoints url openai https://api.openai.com/v1
set endpoints protocol openai responses
```

The Claude API speaks `chat` too, once the headers of its endpoint name the page,
`anthropic-dangerous-direct-browser-access: true`, and give `anthropic-version: 2023-06-01`, which
its list needs. Every MCP server sends its key as a bearer token unless `auth-header` says: `set mcp
auth-header exa x-api-key`, `set endpoints auth-header azure api-key`, `set endpoints auth-header h
'Authorization: Token'`. Every endpoint and every MCP server sends the headers it is given besides,
`Name: value` pairs split by `;`, to it alone.

Every model holds its own request parameters, sent under their OpenAI names and only when set.

```
! every model, by endpoint, with its settings
show models
set chat model prod/qwen3:8b
set models temperature prod/qwen3:8b 0.6
set models reasoning_effort prod/qwen3:8b high
! every setting of that model, gone
no models prod/qwen3:8b
! the seconds to start answering, 120 by default
set endpoints timeout prod 300
```

The parameters are `temperature`, `top_p`, `top_k`, `min_p`, `max_tokens`, `presence_penalty`,
`frequency_penalty`, `seed` and `reasoning_effort`, the last one as the template of the model
reads it. A reply streams until it ends or the turn is stopped. Several conversations answer at
once, each turn in its own; a server running one model at a time loads them in turn.

One line under the turn says what the system does now, redrawn at every frame of the screen:

- `Preparing - 12 ms` while a round reads its model and gathers its tools
- `Waiting for local - 842 ms` until the first chunk
- `Retrying hf - Error 429 - Attempt 2/1800 - 3,200 ms` while a busy endpoint is asked again
- `Thinking - 1,040 ms - 52 tokens - 50.0 t/s` while the model reasons
- `Writing - 3,518 ms - 177 tokens - 51.3 t/s` while it answers
- `Calling config - 312 ms` while a call streams
- `Running config - 47 ms` while the tool runs
- `Round 2 - Running config - 47 ms` from the second round on

It hides while a question of the turn stands, as the time the user takes is none of the system. A
request asks the endpoint to count its tokens, `stream_options` of OpenAI, one per chunk counting
until it does. Once the turn ends, what it spent stays with its answer, beside its time:
`1,204 tokens - 51.3 t/s - 23,512 ms`, the rate over the generation alone, the last figure the whole
time of the system.

## MCP servers

KiSS calls the tools of any MCP server over Streamable HTTP, with the official TypeScript SDK. A
server speaking the 2026 protocol is talked to in it, an older one through the `initialize`
handshake. One client per server lives while its url, key, headers and timeout stay, with the tool
list it serves, asked again on that client as each turn starts, so a tool a server gains shows from
the next turn on; a request that fails drops the client, and the next use connects again. A server
connects as soon as the configuration names it, at load or on a change, so the first round that
needs its tools finds them listed; a round waits only for a server still connecting, and its line
names it, `Preparing mcp sandbox - 812 ms`. A server that fails sits out the rest of the turn, its
error told to the model every round, and the next turn tries it again: a server that never answers
costs one wait a turn, the tools of a turn the same from round to round.

A server is reached directly at its url, or through the route of its name on a KiSS server: `set
mcp proxy exa backend` reaches `exa` at `proxy/exa` beside the url of `backend`, with the key and
headers of `backend`, the url and key of `exa` waiting unused, as for an endpoint; `no mcp proxy
exa` goes direct again. One item, one set of tools, either way. The proxy
names another server of the page that has no proxy itself, and the model sets it only once the user
agrees, as a url. The backend the page comes from is `backend` in the `client.conf` the project
ships, `set mcp url backend mcp`, wherever it runs.

A server may ask the user, the form mode of MCP elicitation, under its message: each text field of
its form asks in the masked field of the page, `Value of server key of backend`, and each yes or no
field asks the user to confirm; the call goes again with the answers, which never reach the model.
Leaving a field empty, or saying no, cancels the form; a form with no field, or with a field of
another kind, is refused. A KiSS server asks this way for `set server key` written alone by the
model, and to let a route take a url the model gives it.

```
set mcp url sandbox https://example.com/mcp
! a masked field asks for the token
set mcp key sandbox
! every tool, by who serves it
show tools
! the settings of the tools named bash
show tools | include bash
! the model never sees it
set tools use str_replace off
! the argument every folded call shows, the default
set display preview-argument description
! the one of a tool over it; a call without it shows its first argument
set tools preview-argument bash_tool command
! every setting of that tool, gone
no tools bash_tool
! the tool rounds a turn takes at most, 1000 by default
set tools max-rounds 2000
! the seconds a call may take, 310 by default
set mcp timeout sandbox 600
```

The key goes as `Authorization: Bearer <token>`. A browser cannot set that header on a WebSocket,
so WebSocket and stdio servers stay out of reach. A server must allow the origin of the page
(CORS). A call runs until it answers, within the timeout of its server, or until the turn is
stopped; connecting and listing the tools take five seconds at most.

`show version` names the system, the page, then every KiSS server the page reaches, by the line
each gives of itself in its `serverInfo`, as a router lists its components; another MCP server
gives none:

```
! KiSS Operating System
! page 8f7bdeb (2026-10-11)
! server forge c6a4f05 (2026-10-11) on linux
! server studio c6a4f05 (2026-10-11) on windows
! server nomad c6a4f05 (2026-10-11) on macos
```

A call waits 310 seconds by default, ten more than the 290 a shell of the KiSS server runs at most,
as BACKEND.md sets it: the server ends its own run first and the model reads why. The two figures
miss 300 by ten on purpose, so whoever wonders why they do not fall on it finds the gap is meant.

## Conversations

The page is attached to a KiSS server or detached from it, two worlds that never mix.
Attached, to the server the item `backend` of `mcp` names, the page shows
the conversations of that server alone, follows them by one stream of events, a WebSocket, the Conversations API
of BACKEND.md, and never writes one. A message goes to the server, which runs the turn, holds the
conversation and writes it, the browser closed or not; retry, edit, versions, pin, delete and Stop
go to the server, and every page following it sees them. Every CLI line runs on the server, its
completion coming from it as it is typed, except `attach` and `detach`, which the page runs itself.
Detached, the default, the page holds the conversations of the browser, answers them itself and
runs every line. `attach` and `detach` swap the two, and the browser keeps which one they left,
apart from the configuration: `write` writes the configuration of where it runs. Until either runs,
a page with a `backend` attaches to it, as the page a KiSS server serves, which names that server
`backend`. Every conversation leaves the page as it attaches or detaches, those of the browser
coming back once it detaches. The key
of a server the page reaches is asked on the page, in the open conversation of a detached page, in
none on an attached one. An attached page is a machine of KiSS beside its server: `client hostname`,
four letters the page draws the first time it opens and the browser keeps, is the name the agent of
the server calls its config tool by, `<hostname>_client_config`, the page running the lines of
such a call itself, with the rights of a model, the questions they ask shown on that page alone. A call the server asks about shows on every page following the
conversation as the consent card, and a form a tool of the server asks the user to fill as the cards
of its fields, a secret in the masked field, a yes as a confirmation; the first page to answer sends
it to the server, which withdraws the question from every page. The line under an answer the
server writes is its pulse, read on the clock of the page. The stream is a WebSocket, which takes none of
the six connections a browser opens to a site for the requests of its tabs; one silent for 45 seconds
opens again, and a server that speaks another protocol of the events is followed no more, the console
telling why.

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
another by a prefix of its id, of the conversations, or of a configuration, in a card the user saves
from: a browser saves a file on a click only, so the model never puts one on the disk by itself. A
configuration, the running-config, the startup-config or a save, is its `set` lines with the values
of its secrets, the browser holding them in clear anyway, so a copy to another browser needs no key
typed again; they go to the disk of the user alone, the output naming the file only. A word naming a
configuration names no conversation. A file is named after what it holds, then the minute for a
configuration and the conversations, `running-config 2026-10-10 14-32.conf`, `conversations
2026-10-10 14-32.json`, and the title for one conversation, `conversation Two words.json`. `import`
asks for a file in a card the user picks from and reads it by what it holds: a JSON file adds its
conversations whose id is new, the others skipped and told; any other file runs its lines as more
lines of the batch, whole or not at all, with the rights of whoever asked, the firewall included,
the way lines pasted after a `/` run, its output opening with `! ran 5 lines of the file`; a file
that holds nothing fails. Neither changes a conversation, the one shown included: an export reads
the conversations as they settled, without the call that exports them, and an import only adds. To
keep an export out of the context, edit the message that asked for it.

```
show conversations
! this conversation
export
! another, by a prefix of its id
export 3f2a
! a configuration, secrets included
export running-config
! every conversation
export conversations
import
! once the user confirms it
erase 3f2a
! every one but the pinned ones
erase conversations
! a save
erase work
```

One file format holds one conversation or many, marked `kiss`, every branch, every id and every pin
in it, read whole or refused with where it goes wrong: every conversation once, every entry id once,
every parent before its child, a leaf that ends a branch. The sidebar lists New chat first, lit
while no conversation is open and staying at the top while the list scrolls, then the conversations,
the last answered first: the pinned ones under Pinned, the others under the day of their last
answer, today, yesterday, then their date, in the language of the browser. Beside each, a pin pins
it or puts it back under its day, and a cross deletes it once the user confirms, Enter or OK, Escape
or a click elsewhere cancelling; while the model answers in it or a question waits in it, a dot of
the accent stands at its right, blinking at 5 Hz for the question, still under
`prefers-reduced-motion`, giving way to the pin and the cross on hover. Every item runs from the
left edge of the page to the scrollbar, and a title too long fades out rather than losing letters to
an ellipsis. A conversation dates from its last answer: a command, a version shown, a pin or a title
dates nothing.

The browser keeps the conversations in IndexedDB, as they settled, the saves in `localStorage`
under `kiss.saves`, the startup-config under `kiss.startup`, and the width of the sidebar under
`kiss.sidebar`.

## Rendering

The thinking and the reply render as Markdown, each plain on demand, `set display render reply
plain`: a plain block reads as written, in monospace. The thinking keeps its small size and the font
of the page, its headings at its size. Markdown renders with remark and rehype: GitHub flavored
Markdown, LaTeX through KaTeX, code highlighted in every language lowlight knows, under a head
naming it with a copy button. Raw HTML shows as the text it is, a link to anything but a web page or
a mail address keeps its text only, and a wide table scrolls in its own box. While a reply streams,
every block but the last renders once and is kept, and the page renders at most once per frame.

## Style

The page style is plain CSS, its tokens on `:root` in `tokens.css`, one theme and no other: three
colors, `bg`, `fg` and `accent`, every other color derived from them, shades of the background,
shares of the text, the accent drawn toward the text, and hues at one lightness that follows the
text, with the most chroma the screen shows at any lightness without bending the hue, so the three
set a whole theme, dark or light alike; `radius`, `font`, `mono`, `size-primary` for the chat and
`size-secondary` for everything around it, the only two sizes of text but `size-title`, the title of
the page, each read floored, every icon at the primary size, `width`, `bubble-width`,
`sidebar-width` within `sidebar-min` and `sidebar-max`; every scrollbar thin, its thumb a line. The
module `style` sets it: a token of a value of its own is a key, typed by its default, a color, a
length or a font, and `show style` lists them with their defaults, as set lines; a derived one
follows the tokens it reads, and only a sheet sets it alone. `show css` lists the CSS of the page by
component, the rules of a component under its name, each with its scoping class, `svelte-composer`
for `Composer.svelte`, so a selector read there aims at that component alone, and the rules of none
under `page`; named sheets restyle anything over it. The sidebar follows its edge for the whole
drag, within those bounds, it closes once the pointer goes below half the least width, and opens
again once it reaches that width, from the left of the page too; the browser keeps both, the width
and whether it is closed.

```
! the tokens and their defaults
show style
set style accent 'oklch(0.6 0.2 250)'
set style bg #faf9f5
! with bg, a light theme
set style fg #141413
set style width 64rem
! back to the background of the page
no style bg
! the rules of the composer
show css | include composer
! the composer alone
set style sheet input 'form.svelte-composer { max-width: 64rem }'
```

## Site defaults

An optional `client.conf` next to `index.html` gives a site its defaults, for instance the LLM a
fresh page talks to. It holds `set` lines only, read at every page load, over the factory
configuration and below every save of the user. The KiSS server serves the one of the directory it
runs in, else `server/client.conf` it embeds: its backend at the relative URL `mcp`. Anyone who can
load the page can read it, so a key set there is public.

```
set endpoints url prod https://example.com/v1
```

## Build

```
./buildwebui.sh
```

It installs, formats, type checks, runs the laws, then builds `webui/dist/index.html` and its gzip
`tools/public/index.html.gz`, which the server embeds.
