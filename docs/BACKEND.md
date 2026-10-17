# KiSS server

The reference of the backend: one binary, `server`, that runs commands, views, edits and creates
files on the machine it runs on, for KiSS or any other MCP client.

## Commands

```
commands
  set <module> <key> [item] <value>
  no <module> <key> [item]
  no <module> <item>
  show <module> [key] [item]
  show <module> <item>
  show running-config [all]
  show startup-config
  show diff <config> <config>       running-config or startup-config, each
  show version
  show logging
  show conversations
  copy running-config startup-config
  copy startup-config running-config
  write [memory]
  erase running-config|startup-config
  erase conversations|<id>          every one but the pinned ones, or one by its id
  <command> | begin|count|exclude|include <pattern>
  exit, quit                        the console only: stop the server

modules
  server
    listen-address <address>        127.0.0.1
    listen-port <1..65535>          8089
    hostname <word>                 the host name
    key <secret>                    required off loopback
    working-directory <path>        the start directory
    log-level errors|informational|debugging
                                    debugging
    log-buffer <bytes>              65536, the log show logging keeps
  shells
    command <item> <line>           bash, zsh, powershell, pwsh, where found
    timeout <item> <seconds>        290
    output <item> <bytes>           65536
  files
    max-output <bytes>              65536, the text view returns
    max-load <bytes>                52428800, a file loaded whole
    max-megapixels <count>          100, decoded at most
    max-dimension <pixels>          1920, the longest side sent
    jpeg-quality <1..100>           85, of a JPEG made again
  proxy
    url <item> <url>                a server proxied at proxy/<item>
    key <item> <secret>
    auth-header <item> <Header[: Scheme]>
                                    Authorization: Bearer
    headers <item> <Name: value; ...>
    timeout <item> <seconds>        300
  endpoints
    url <item> <url>                an LLM the agent talks to
    protocol <item> chat            chat
    key <item> <secret>
    auth-header <item> <Header[: Scheme]>
                                    the one of the protocol
    headers <item> <Name: value; ...>
    timeout <item> <seconds>        120, the endpoint stays silent at most
    max-retries <item> <count>      1800, on a status that passes
    retry-pause <item> <seconds>    2
  chat
    model <endpoint/model>          the one model of the one endpoint
    system-prompt <text>            none
  models
    temperature, top_p, top_k, min_p, max_tokens, presence_penalty,
    frequency_penalty, seed, reasoning_effort <endpoint/model> <value>
                                    sent only when set
  mcp
    url <item> <url>                an MCP server the agent calls
    key <item> <secret>
    auth-header <item> <Header[: Scheme]>
                                    Authorization: Bearer
    headers <item> <Name: value; ...>
    timeout <item> <seconds>        310, a call takes at most
  tools
    max-rounds <count>              1000
    use <tool> off|consent|on       consent, the tools of the server on

tools
  <hostname>_server_config          description lines
  <hostname>_server_<shell>         description command
  <hostname>_server_view            description path [view_range]
  <hostname>_server_str_replace     description path old_str new_str
  <hostname>_server_create_file     description path file_text
```

## The server

`server` is an MCP server over Streamable HTTP and a process launcher. One runs on each machine that
works for the model: a GPU server, a Windows box, a laptop. A KiSS page drives as many as its
`client.conf` names, each under its own name, so the page never changes when a machine joins.

The C++ holds mechanisms only: HTTP, MCP, the process layer of each OS, decoding and truncation of
output, the security checks. Every behavior is data in `server.conf`: which shells exist, how each
one starts, its limits, the working directory. A new shell, a remote host or a container is a line
of configuration, never a rebuild.

The binary also serves the KiSS page, so one download gives a working harness: start `server`, open
`http://127.0.0.1:8089/`.

## The repository

Front and back are one project and peers, `webui/src/` and `server/`, laid out alike: the same
`prompts.json`, `engine/`, `commands/`, `modules/`, `tools/` and `lib/` on both sides, a file per
concept, the engine file for file. A feature adds a file; no file grows with the features.

```
webui/                     the page, as FRONTEND.md describes it
server/main.cpp            the start: prompts, defaults, startup-config, console, listener
server/prompts.json        what a model reads of the server: its tools, its modules
server/engine/             the CLI: text, parse, filter, near, schema, values, registry, archive,
                           run, complete, and the keychain with its seal
server/commands/           one file per command, registered by existing
server/modules/            one file per module: its keys, rules, how it applies to the server
server/tools/              one file per tool source, registered by existing
server/mcp/                the MCP: the entry of both eras, the JSON-RPC answers, the methods
server/lib/listener.cpp|h  the socket, moved when server listen-address or listen-port changes
server/lib/http.cpp|h      the routes and the checks of Host, Origin and the key
server/lib/proxy.cpp|h     the proxy of the servers the proxy module names
server/lib/console.cpp|h   the console of the interactive mode, opened on a terminal in the
                           foreground
server/lib/term.h          the terminal of the console, per OS: raw mode, a key, the width
server/lib/log.cpp|h       the log: [Module] Line
server/lib/job.cpp|h       spawn, pipes, tree kill, per OS
server/lib/output.cpp|h    UTF-8 repair and head plus tail truncation
server/lib/file.cpp|h      the files the tools on files reach: a path, a text loaded whole, a write
                           in place, a string found; a file the server keeps, replaced whole
server/lib/conversation.cpp|h
                           the conversations as a file, read as the page reads it
server/lib/library.cpp|h   the conversations of the server, a file each in conversations/, and
                           what each follower is to apply
server/lib/sync.cpp|h      the Conversations API: the stream of events and the actions of a page
server/lib/agent.cpp|h     the agent: a turn per conversation, on its own thread
server/lib/pulse.cpp|h     the clock of a turn, the twin of pulse.ts of the page
server/lib/api.cpp|h       the client of the endpoints: the model list, a reply streamed
server/lib/mcp.cpp|h       the client of the MCP servers the agent calls
server/lib/remote.cpp|h    a server an item names: its url, key and headers, and their rules
server/protocols/          one file per protocol an endpoint speaks: chat, messages, responses
server/lib/image.cpp|h     an image as a model is sent it: sized, turned, resized, made a JPEG
server/lib/tools.cpp|h     the tools the server serves, each source registered by its file
server/lib/prompts.cpp|h   the texts of prompts.json, read once
server/lib/utf8.h          the UTF-8 boundary of Windows: argv, fopen
server/tests/seal.cpp      the laws of the seal, built and run with the server, a broken law failing
                           the build
server/tests/files.cpp     the laws of lib/file.cpp, built and run the same way
server/tests/conversation.cpp
                           the laws of lib/conversation.cpp and lib/library.cpp, built and run the
                           same way
server/tests/protocol.cpp  the laws of the protocols, built and run the same way, each body the very
                           bytes the page writes, as webui/tests/bodies.jsonl holds them for both
server/tests/mcp.cpp       the laws of lib/mcp.cpp against a server of the test, built and run the
                           same way
server/tests/agent.cpp     the laws of lib/agent.cpp, a turn against an endpoint and MCP servers of
                           the test, built and run the same way
server/server.conf         the factory configuration the binary embeds
server/client.conf         the client.conf the server serves when its directory has none
vendor/                    cpp-httplib, LibreSSL for the TLS of Windows, yyjson, stb
tools/public/              index.html.gz, the page the binary embeds
tools/xxd.cmake            embeds a file as a C array
tools/version.cmake        the git commit of the build
CMakeLists.txt             the server, built without npm, into build/server
buildwebui.sh              builds the page in webui/ and gzips it into tools/public/, optional,
                           first
build.sh                   builds the server with CMake alone
build.cmd                  the same on Windows, with Visual Studio Build Tools and Ninja
format.sh                  clang-format over the C++, vendors excluded
```

Every file has one source: the binary embeds `server/client.conf` and `server/server.conf` as
they are. `tools/public/index.html.gz` is the one built file under git, so a C++ developer builds
the server with CMake alone; the page build writes it, gzipped deterministically.

The stb headers of Sean Barrett, which llama.cpp uses as well, decode, resize and encode images;
their implementations compile once in `vendor/stb/stb.c`, the decoder reading PNG, JPEG and GIF
only.

There is no process library in the vendors. sheredom/subprocess.h, which llama.cpp uses, offers no
process group on POSIX and no Job Object nor suspended start on Windows, so it kills the direct child
only and grandchildren outlive a timeout. `job.cpp` owns that layer.

## Configuration

### The engine

The configuration runs on the engine of the page, rewritten in C++: the same words, the same
batches, the same laws. A key is two fixed words, a module then a key; a key of a collection takes
the item name next, and the value comes last. A word that names something already there may be any
prefix that names it alone, the way Cisco IOS reads a line: a command, a module, a key, a filter,
the value of an enum (`set server log-level inf` sets `informational`), `all` and `memory`, the item
of `no` and `show`, and a configuration (`copy run start`); the configuration keeps the whole word.
A word that may be new is written whole: the item of a `set`, and every value but the one of an
enum.

A batch compiles every line before the first one runs, runs them on a draft of the running
configuration, checks the rules of every module on the draft, then replaces the running
configuration and applies the modules whose keys changed. A failure at any step applies nothing.

The commands are those of the page that fit a server, with the same words and the same answers:
`set`, `no`, `show <module>`, `show running-config [all]`, `show startup-config`, `show diff`
between the running-config and the startup-config, `show version`, `copy` between them, `write
memory`, or `write` alone, IOS's way to save, and `erase running-config`, back to the factory
configuration, or `erase startup-config`, `server.conf` gone so the server starts on the factory
configuration; `show conversations`, and `erase conversations`, every one but the pinned ones, or
`erase <id>`, one by its id, a prefix being enough; and one of its own, `show logging`. `copy`, `write` and `erase` run alone, on a line
of their own, as on the page, so a batch never both writes and erases `server.conf`. A save or an
erase of `server.conf` happens once the batch holds, so a batch that fails or waits for the user
writes nothing. A line takes a filter
after a pipe, `begin`, `count`, `exclude` or `include`, as IOS does. A secret never prints: `show`
writes `! server key is set`.

Two roles send batches. The administrator writes `server.conf` and the command line. An MCP client
runs the config tool, whose lines a model may write, so it writes no value of a secret: `set` of a
secret alone asks the user to type it. And as the page asks the user before its model copies or
erases a configuration, a `copy` or a `write` to the startup-config and an `erase` from an MCP
client wait for the user's yes, whatever the client lets the tool do.

A module registers itself by existing, with its keys, its rules and what it applies: `server`
rebinds its socket, `shells` rebuilds the tool list, `files` bounds what its tools return. The
engine knows no module by name, so a module joins without a change to the engine.

### server.conf

`server.conf` holds set lines in the grammar of `client.conf`: a module, a key, the item name for a
collection, then the value. `!` starts a comment, `no` removes an item or a value.

The startup-config is `server.conf` of the directory the server runs in: the root of the repository
during development, where git ignores it, the working directory of a systemd unit, the directory of
a prebuilt binary. Several servers run on one machine from several directories. At start the file
runs as IOS boots: a line the parser refuses stays out, then the rest goes block by block, an item
with all its lines, a block a rule refuses staying out and tried again once others entered, as one
item may name another. The log tells each one, `server.conf skips shells z: shells: shells command z
is not set`, and the server starts on the rest, never stopped by a bad line. The factory
configuration holds the rules of every module, or the binary does not start. The result is the
running configuration, in memory; `copy running-config startup-config` writes it back to the file
whole.

A value resolves through layers, the first that holds it winning:

1. the running configuration;
2. the factory configuration `server/server.conf`, embedded in the binary, only what differs from a
   reset value;
3. the reset value the key declares: the host name of the machine for `server hostname`, the start
   directory for `server working-directory`, `8089` for `server listen-port`.

A batch answers with its change, every register the running layer moves, read through the layers
before and after, a register of an item existing while a layer names the item, as on the page.

### One file for every machine

One `server.conf` serves every machine: it is copied unchanged to `forge`, `studio` and `nomad`.
What differs from one machine to another is found on each, never written per OS: a shell whose
program is not found on the PATH stays out, the log and `show shells` saying which, `! shell zsh
left out, zsh is found nowhere`, `! shell bash runs /usr/bin/bash`. The factory configuration
therefore lists every candidate shell and each machine serves the ones it has.

### Modules

| key                        | kind   | meaning                                                         |
| -------------------------- | ------ | --------------------------------------------------------------- |
| `server listen-address`    | string | address to listen on                                            |
| `server listen-port`       | number | port to listen on                                               |
| `server hostname`          | string | the prompt and the prefix of every tool name, as IOS's hostname |
| `server key`               | secret | bearer key, required off loopback                               |
| `server working-directory` | string | working directory of every tool, the start directory by default |
| `server log-level`         | enum   | `errors`, `informational` or `debugging`, what the log tells    |
| `server log-buffer`        | number | bytes of the last lines of the log `show logging` keeps         |
| `shells command <item>`    | string | command line that runs a script read on stdin                   |
| `shells timeout <item>`    | number | seconds a call may run                                          |
| `shells output <item>`     | number | bytes of output a call returns                                  |
| `files max-output`         | number | bytes of text `view` returns, its head and its tail             |
| `files max-load`           | number | bytes of a file a tool loads whole, an image or a text to edit  |
| `files max-megapixels`     | number | megapixels an image may declare before it is decoded            |
| `files max-dimension`      | number | the longest side of an image sent, in pixels                    |
| `files jpeg-quality`       | number | the JPEG quality of an image made again, 1 to 100               |
| `proxy url <item>`         | string | URL of a server this server proxies at `proxy/<item>`           |
| `proxy key <item>`         | secret | key sent to that server                                         |
| `proxy auth-header <item>` | string | the header of the key, `Header` or `Header: Scheme`             |
| `proxy headers <item>`     | string | more headers sent to it, `Name: value; Name: value`             |
| `proxy timeout <item>`     | number | seconds it may stay silent                                      |

A command line splits into arguments on spaces, double quotes grouping. On Windows it reaches
`CreateProcessW` unchanged, which is the command line Windows expects.

### The factory configuration

Every key powers on at the reset value the tree above gives it. The factory configuration
`server/server.conf` holds only what differs from them: the shells, a command line each.

```
set shells command bash bash -s
set shells command zsh zsh -s
set shells command powershell powershell -NoProfile -NonInteractive -Command "<boot>"
set shells command pwsh pwsh -NoProfile -NonInteractive -Command "<boot>"
```

Every shell runs at the reset values of its other keys, `timeout` 290 seconds and `output` 65536
bytes, until a line sets its own. `<boot>` stands for the PowerShell bootstrap below, written in
full in the file.

Each machine serves the shells it has: bash on Linux, zsh and bash on macOS, Windows PowerShell 5.1
on Windows, and PowerShell 7 wherever it is installed; on Windows the bash of Git Bash or of WSL
joins when its program is on the PATH.

## Conversations

The conversations of a server live in `conversations/` of the directory it runs in, next to
`server.conf`, a file each, `<id>.json`, in the format the page exports: that conversation alone,
named by its id. The server reads them all as it starts; a file that does not read as the page
reads it is logged and left out. Copying a file out exports a conversation, copying one in imports
it at the next start. `show conversations` lists them, the newest first, and `erase` removes a
conversation with its file.

The pages that show them follow them, as many as are open, and never write one: the server alone
changes a conversation, its file written whole again through a file beside it renamed over it, then
tells every page. A page shows what the server holds and sends what its user does; the turns, the
tools, the MCP servers, the questions to the user and the files are all the server's. The
conversations a page keeps in its own browser stay there.

### Conversations API

All of it lives under `/conversations`, behind the key of the server as the MCP is: `Authorization:
Bearer <server key>`, 401 without it, and the same CORS, the stream carrying the key in its first
message. Nothing else of the server serves a
conversation.

#### Reading: `/conversations/events`

A WebSocket. A browser sets no header on one, so its first message holds, as JSON, the headers a
request would carry, the key among them, `{"Authorization":"Bearer <server key>"}`; the server
closes a stream it refuses with 1008, policy violation, and why. Every next message is the server's:
events, one JSON object per line, an empty message after 15 seconds without one, so a page that
hears nothing for 45 seconds knows its stream gone and opens another; the server pings every 15
seconds and lets go of a page silent for 45. A WebSocket counts in none of the six connections a
browser opens to a site in HTTP/1.1, which all its tabs share, so no number of pages blocks
another. The server keeps no log
of events: it holds, for each page, what that page has, and sends what it lacks, so a page that
comes back gets every conversation as it stands. The first event sets every conversation, with the
protocol of the events and the moment of the server on its clock, in milliseconds since 1970; a
page that does not read that protocol stops following the server and says so.
`?client=<hostname>` names an attached page, a-z, 0-9 and `_`, a policy violation otherwise: while its stream lasts,
the agent calls the config tool of the page as `<hostname>_client_config`, and the first event names
the client, unless another page holds that name, which the first event then leaves out.

```
{"set":[],"value":{"protocol":1,"client":"abcd","now":1792220666396,"conversations":[...]}}
```

Every next event changes what the page holds at its path, conversations by id, then the keys and
indices of the file format:

| Event | What it does |
|---|---|
| `{"set":["<id>"],"value":{...}}` | a conversation that came or changed, whole |
| `{"set":["<id>"],"value":null}` | a conversation gone |
| `{"add":[...],"text":"..."}` | the text a string of a turn gained |
| `{"add":[...],"value":{...}}` | a round or a call that came to a turn |
| `{"set":[...,"calls",<n>],"value":{...}}` | a call whole, once its outcome came |
| `{"set":[...,"rounds",<n>,"opaque"],"value":{...}}` | the items the protocol of a round keeps, whole, as one more came |
| `{"set":["<id>","turn"],"value":{...}}` | the pulse of the turn that answers, `null` once it ended |
| `{"set":["<id>","asking"],"value":{...}}` | the question the turn asks the user, `null` once settled |

A turn sends what it gains, never the conversation again; a change of anything else sends the
conversation whole, with its turn and its question after it.

```
{"add":["<id>","entries",3,"rounds",0,"text"],"text":"Hel"}
{"add":["<id>","entries",3,"rounds"],"value":{"reasoning":"","text":"","calls":[]}}
{"set":["<id>","entries",3,"rounds",0,"calls",0],"value":{...}}
```

The pulse is the line a page shows under the answer, its moments on the clock of the server:
`{"round","phase","name","status","attempt","attempts","since","first","last","tokens"}`, the phase
one of `preparing`, `waiting`, `retrying`, `thinking`, `writing`, `calling` and `running`, since
when it began, the endpoint or the tool it names, the status, attempt and attempts of an endpoint
asked again, and the first and last chunk of the round with its tokens, `null` before them. It
changes as the turn does; a page reads its moments on its own clock from `now`, and runs the line
itself.

The question is `{"kind":"call","tool":...,"args":...}` for a call in `consent`, or
`{"kind":"form","server":...,"input":...}` for a form a tool asks the user to fill, `input` the MCP
input request of an `elicitation/create` form (see The agent).

A comment line comes each second the stream stays quiet; a page that hears nothing for three
seconds counts the connection as gone and opens the stream again. The stream ends when the
listener moves.

#### Acting: `POST /conversations/<action>`

A JSON body; the server applies the action and every page, the one that sent it included, sees the
outcome through its stream. An action answers no state.

| Action | Body | What it does |
|---|---|---|
| `send` | `{text, id?}` | a message after the thread, a turn answering it |
| `command` | `{input, id?}` | a line this server runs as its config tool does, its forms asked as the turn of the conversation, its output after the thread |
| `complete` | `{line}` | answered `{words}`: the words that complete a line being typed, as Tab and `?` of the console |
| `edit` | `{id, entry, text}` | a version beside a message of the user, a turn answering it |
| `retry` | `{id, entry}` | a new answer to a message, or beside an answer |
| `browse` | `{id, entry}` | the thread goes to the last entry written under that one |
| `stop` | `{id}` | the turn stops |
| `answer` | `{id, verdict}`, `{id, response}` or `{id, output, ok}` | the question of the turn settled, the first answer winning |
| `pin` | `{id, pinned}` | the conversation pinned or not |
| `drop` | `{id, entry}` | a command entry leaves, its conversation going when it was the last one |
| `erase` | `{ids}` | the conversations go, files and all |

`send` and `command` without `id` open a conversation. The verdict is `once`, `always` or `refuse`;
the response is `{"action":"accept","content":{...}}` or `{"action":"cancel"}`.

| Status | When |
|---|---|
| 204 | applied |
| 200 `{id}` | applied by `send` or `command`, with the conversation it went in |
| 200 `{words}` | `complete` |
| 400 | a body that does not hold: not JSON, a field missing or of the wrong kind, an entry of the wrong role |
| 404 | a conversation or an entry the server does not hold |
| 409 | a turn runs, for every action on its conversation but `stop`, `answer`, `pin` and `erase`; or `answer` while no such question stands |
| 500 | a file not written |

Every refusal tells why in its body, one line of text. The log names each action and its
conversation, never its body.

#### Laws

- A page that applies every event it got, in order, holds what a page that just came gets, through
  any run of changes, read at any pace: the build checks it against thousands of random changes of
  every kind, read by three pages at three paces.
- An entry that settled never changes: every edit, retry and version adds entries and moves the
  leaf.
- One turn at a time answers in a conversation, and the server alone writes it.
- No secret leaves the server: the file, the events and what the model reads are sealed.
- The turn and its question live in memory alone; the file holds what settled, written before
  every call of a turn.

## Running

`server` takes no argument. Its stdin tells how it runs: a terminal it runs in the foreground of
opens the console; anything else, `/dev/null` under systemd, a Windows service, `nohup`,
`< /dev/null` or a start with `&` from a shell, runs it as a daemon that leaves stdin alone.

Its log goes to stderr, one line per event in the form of every GGML project of the house,
`[Module] Line`, and opens on the version. A server tells everything that happens, as the console
of a router does by default: `server log-level` sets how much, at once, no restart.

- `errors`: what fails or is refused, a request without its key, a call refused or killed, a
  listener that cannot bind.
- `informational`, and the above: the events, a client that connects, each call with its why as it
  starts and its outcome with its duration as it ends, each change of the configuration.
- `debugging`, the default, and the above: the detail, each request with its method, address and
  era, every other argument of a call, a text by its first line, and the bytes a call sends back.

`show logging` prints the last lines of the log, as IOS keeps them with `logging buffered`:
`server log-buffer` sets how many bytes stay, every line of the start included, so the console, KiSS
through the config tool, and the model asked to diagnose read what happened without the terminal of
the machine.

```
[Server] KiSS Operating System, server 5633ee7 (2026-10-11) on linux
[Server] Listening on http://127.0.0.1:8089
[MCP] server/discover from 192.168.1.20, 2026-07-28
[MCP] Client kiss 5633ee7 connects from 192.168.1.20
[MCP] tools/call from 192.168.1.20, 2026-07-28
[MCP] Call forge_server_bash: Builds the server
[MCP] Call forge_server_bash: command cd kiss && ./build.sh (2 lines)
[MCP] Call forge_server_bash: exit 0 in 5321 ms
[MCP] Call forge_server_bash: 1838 bytes back by SSE
[HTTP] Refused 203.0.113.7: the key is missing or wrong
```

The console is the interactive mode, as the console port of a Cisco router: the prompt names the
server, each line runs as a batch of the administrator, and the log passes above the line being
typed, which it never cuts.

```
[Server] KiSS Operating System, server 5633ee7 (2026-10-11) on linux
[Server] Listening on http://127.0.0.1:8089
forge# show version
! KiSS Operating System
! server forge 5633ee7 (2026-10-11) on linux
forge# set server listen-port 9000
[Engine] Configured by the administrator
- set server listen-port 8089
+ set server listen-port 9000
forge# copy run start
! copied running-config to startup-config
```

The line is edited as in a shell, and completes as IOS does, from the words the engine reads:

- Tab completes the word when one word fits, else the start every fitting word shares, else lists
  them: the words of the engine, the items set, the filters after a pipe, and for a value its
  current one, a secret aside; `?` lists what may follow, or the words the word typed starts, and
  leaves the line as it is. Ctrl-V types the next key as it is, a `?` in a value.
- Left and Right, Home and End or Ctrl-A and Ctrl-E move; Backspace and Delete erase a character,
  Ctrl-U the line before the cursor, Ctrl-K the line after it; Up and Down walk the history.
- `exit` and `quit`, Ctrl-C, and Ctrl-D on an empty line stop the server. They are words of the
  console, not of the CLI, so no MCP client can stop it.

## Tools

A tool name is `<hostname>_server_<item>`: `forge_server_bash`, `studio_server_powershell`,
`nomad_server_zsh`, the machine, then its role, then the tool, so a model never takes a tool of a
server for one of a page. The hostname is by default the one of the machine, lowercased with every
character outside `[a-z0-9]` made `_`, which keeps the name valid for every model API. Two machines
never collide, and KiSS filters a tool by its name with `set tools use <tool> off`.

A description is short: what the tool does and the machine it acts on, `Run a bash command on
forge`. A cut output, a timeout or a resized image, the result tells. Only the hostname changes it,
so the tool list stays the same from turn to turn and the prompt cache of the model holds. Every
text a model reads of the server lives in `server/prompts.json`, as every one of the page lives in
`webui/src/prompts.json`: each tool with its description and the JSON Schema of its arguments,
`{hostname}`, `{platform}`, `{shell}` and `{modules}` standing for what only the running server
knows, and each module with what it sets. A module that has no text there stops the server as it
starts.

Every tool takes `description` first, why the call is made, then what it does: its schema lists it
first and says so, for a model to write the why before the what. A model still writes its arguments
in the order it likes, so KiSS shows `description` on a folded call by its name, wherever it stands:
`display preview-argument description` is its default, which `tools preview-argument <tool>`
overrides for one tool. The schema of a tool is its law, as JSON Schema reads it: a key outside it,
a required key missing, a string property given something else, or a string shorter than its
`minLength` refuses the call before it runs, with `Unknown argument <key>`, `<Key> argument
required` or `<Key> argument is a string`, so a misnamed argument never runs as if it were absent.
A string the tool needs is never empty because its schema says `minLength` 1, and a string that may
be empty says nothing.

A result is plain text in sentences, no prompt and no `!`: the CLI is the config tool alone. The
model wrote the call and keeps it in its context, so a result never echoes it.

### The agent

The server runs the agent of KiSS itself, the twin of the one of the page, with no browser open: a
message a page sends, `POST /conversations/send`, starts a turn on its own thread, which answers
round by round, each round reading the running configuration as it stands, until a round calls
nothing or `tools max-rounds` is reached. The modules it reads are those of the page: `endpoints`,
`chat`, `models` and `tools`, with the same keys.

It speaks the three protocols of the page, each the twin of its file there, writing the very bytes
it writes: `chat`, OpenAI chat completions, the open standard of llama.cpp, vLLM and Hugging Face;
`messages`, the Messages API of Anthropic, its thinking blocks signed and sent back as they came,
and `responses`, the Responses API of OpenAI, stateless, its encrypted reasoning sent back the same
way. What a protocol needs back enters the round as the turn streams and reaches every page with
it. A model holds no parameter its protocol never sends, `models top_k` for `responses` say; and
`messages` takes its tokens from `models max_tokens`, else from what the endpoint lists of the
model, else asks for it.

The agent calls the tools of the server directly, each by its own name, `bash`, `view`, `config`,
as a router names its interfaces: `forge_server_bash` is how a client sees it, and the configuration of the
machine, which no other machine reads, names it `bash`. So the factory configuration turns them on
for every machine, `set tools use bash on`, and a tool of another server stays in `consent`.

It calls the tools of the MCP servers the `mcp` module names as well, each by the name its server
gives it, after the tools of the server, a name served twice staying with the first. A server is
reached over Streamable HTTP in the era it speaks: one that answers `server/discover` with
2026-07-28 gets the envelope and the headers of that era on every request, any other the
`initialize` handshake of 2025-06-18 and its session. Its tools are listed once, kept while its url, key and headers stay. A server that fails sits
out the rest of the turn, logged, and is asked again by the next turn or once its url, key or
headers change; a name a server serves that another tool already holds stays with the first. Every
round tells the model what the turn left out, under the system prompt, as the agent of the page
does:

```
These tools were left out during this turn; tell the user:
mcp exa: https://mcp.exa.ai/mcp does not answer: Could not establish connection
mcp twin: tool config left out, KiSS serves it
```

A turn that needs the user asks every page following the conversation and waits, the first answer
settling it and `{"set":["<id>","asking"],"value":null}` withdrawing it from every page; Stop
withdraws it and ends the turn. A call in `consent` poses
`{"set":["<id>","asking"],"value":{"kind":"call","tool":...,"args":...}}`, which
`POST /conversations/answer {"id":...,"verdict":"once"|"always"|"refuse"}` settles: `always` runs
`set tools use <tool> on`, `refuse` answers `The user refused the call`. A tool that asks the user
to fill a form poses `{"set":["<id>","asking"],"value":{"kind":"form","server":...,"input":...}}`,
the MCP input request of an `elicitation/create` form: the `config` tool of the server, a secret to
type or a url to let, under the hostname of the server, or a tool of an MCP server of the 2026-07-28
era whose call answers `input_required`, under the name of that server. `POST /conversations/answer
{"id":...,"response":{"action":"accept","content":{...}}}` settles it, the call running again with
the response, or `{"action":"cancel"}`, which answers `The user answered nothing`. A form whose
fields are other than text or yes fails the call unasked. Every text the user types in a form of an
MCP server is sealed out of every outcome from then on, as the keychain seals its secrets.

The agent calls the config tool of every attached page as well, `<hostname>_client_config`, in
`consent` until `set tools use <hostname>_client_config on`: a page is a machine of its own, whose
configuration this server never reads. Granted, the call poses
`{"set":["<id>","asking"],"value":{"kind":"run","client":...,"lines":...}}`, which every page
following the conversation sees and the page of that hostname alone runs, on its own engine with
the rights of a model, asking its user what its lines ask on that page; `POST /conversations/answer
{"id":...,"output":...,"ok":...}` settles it with what the lines answered. A page whose stream ends
answers its call `The page <hostname> is gone`, and its tool leaves the next round.

The turn writes its answer in the conversation as it streams, the pages that follow it getting each
piece; the conversation is written to its file before a turn, before each call reaches its tool and
once the turn ends, as it settled, so a server that stops mid turn starts again on what it held, a
call sent marked `Stopped before it answered: the tool may have acted`. `POST /conversations/stop`
ends a turn at once, its request closed, no call running after it.

### Shell tools

One tool per shell item that starts. Its arguments are `description` and `command`, the command to
run, which the shell reads on stdin. The result is the output, then its last line, the exit code and
the duration:

```
hello
Exit code 0 (12 ms)
```

A code other than 0 marks the result as an error, as do `Killed after 290 s`, `Cancelled` and
`Cannot start <command>: <reason>`. The run ends after the `timeout` of the item; the model has no
timeout of its own to set.

### The view tool

`<name>_view` takes `description`, `path`, relative to `server working-directory` or absolute, and
`view_range`, the lines `[start, end]` of a text file, from 1, `end` -1 for the last. It gives the
model what a shell gives badly, an image it sees and a text it can cite by line. With `view` on and
every other tool off in KiSS, a machine is open to the model for reading only.

- A directory lists every entry two levels deep, sorted by path, each with the size of all it holds,
  as `find` and `du -sh` would: hidden entries and the content of `node_modules` stay out. The last
  line counts the entries.
- A PNG, JPEG, GIF or WebP image, known by its first bytes, comes back as an image block. A JPEG
  that fits `files max-dimension` goes as it is, and an opaque PNG that fits too when it weighs less
  than its JPEG, as a drawing or a screenshot does. Any other image is made a JPEG of `files
  jpeg-quality` that fits it: resized in linear light with the same scale on both sides, turned as
  its EXIF says, a transparent one laid on white. A text block before it tells the image read and
  the image sent, `Image image/png 6000x4000, 47087504 bytes, sent as image/jpeg 1920x1280, 292632
  bytes`, so the model knows what it sees. A WebP goes as it is when it fits, and is refused when it
  does not.
- Before anything decodes it, an image is refused past `files max-load` bytes, or when its header
  declares more than `files max-megapixels` megapixels, so neither a huge file nor a small one that
  inflates holds the memory of the machine. A photo of 50 MB reaches the model in a few hundred KB,
  well within the 5 MiB a request may weigh on the Hugging Face router.
- A text file comes back with numbered lines, `N<tab>line`, then `300 lines`, or `Lines 10-20 of
  300` for a range. Past `files max-output`, the head and the tail stay, as for a shell. The file
  streams through, so its size costs time and never memory.
- Another binary file, one with a NUL byte near its start, is refused with `Binary file`.

### The str_replace and create_file tools

A shell writes a file through a heredoc, whose quoting differs from bash to PowerShell and breaks on
the text it carries. These two tools write the bytes of a JSON string as they are, the same on every
OS, and a model trained on `str_replace` and `create_file` calls them as it learned them. Both take
`path` as `view` does, a path holding a NUL refused with `Path holds a NUL byte`, as every OS would
cut it to another path. They write in place: the mode, the owner and the links of a file stay, and
an error of the disk on the way, a full one, can leave the file partial, the call failing with
`Cannot write the file: <reason>`.

`<name>_str_replace` takes `description`, `path`, `old_str` and `new_str`, all four required, so a
model that forgets `new_str` is told so and a deletion is a `new_str` written empty. The file loads
whole, up to `files max-load` bytes, and `old_str` must occur in it exactly once, byte for byte: a
CRLF file takes an `old_str` in CRLF. The result says where the change went, so the model can view
the lines around it:

```
Replaced at line 42 of src/main.cpp, 18 -> 25 bytes
```

`Not found in <path>` and `Found 3 times in <path>, old_str must be unique` change nothing, nor do
`Binary file` and `File larger than <limit> bytes`.

`<name>_create_file` takes `description`, `path` and `file_text`, makes the directories the path
needs and writes the file whole, over the one already there: `Created src/new.cpp, 12 lines, 345
bytes`, or `Overwrote` when it existed. `file_text` may be empty: `Created .gitkeep, 0 lines, 0
bytes`.

### The config tool

`<name>_config` takes `description` and `lines`, runs the lines as a batch of CLI on the engine and
answers with its output and its change, as the `config` tool of the page does. Its
description lists every module with its keys and what it sets, from the modules themselves and
`prompts.json`, so the model knows the server before it reads a line of it. KiSS configures every
backend from the page through it, under the consent firewall when the model calls it, and any other
MCP client gets the same CLI. A change of `server listen-address` or `server listen-port` moves the
listener at once, once the address is checked free.

A secret never crosses the context of the model. The administrator writes it in `server.conf` or on
the console. An MCP client writes `set server key` alone: the batch applies nothing, and the call
answers `input_required` with a form of one text field per secret, MCP elicitation of the 2026-07-28
era, its message naming the url each secret goes to. The client asks the user and calls again with
the values in `inputResponses`, out of the context of the model; the batch runs again with them and
answers `! the user typed server key`. The server keeps nothing between the two calls. A url an MCP
client gives a route asks the user to let it, a yes field of the same form, `Let proxy/exa proxy
https://mcp.exa.ai/mcp`, unless the running-config before the batch or the startup-config already
holds it; the secrets of the route go with it once the user lets it, and the form names them, `Let
proxy/b proxy https://b.example/mcp, proxy key b with it`, so a key is never typed again for a url
that changes. A `copy`, a `write` or an `erase` asks a yes field the same way, `Copy running-config
to startup-config for the KiSS server forge`, the batch running again once the user says yes. A line
that writes the value of a secret is refused, and a client that takes no form, every client of the
2025 era, is told the secret goes in `server.conf` or on the console. The text every tool answers
goes through the keychain of the server and reads `<removed>` where the value of a secret of the
server stood, so a shell, `view` or `show logging` never hands a key on; a value the server never
read, a line it refused, is no secret it knows. A module reads a secret `<removed>`: the keychain
alone reads its value beside the engine, as a route sends its key and as the key of a client is
checked. A line `server.conf` refuses is logged by its number, never by its text: `server.conf skips
line 4: unterminated "`.

## The shell contract

The server starts the command line of the item with one pipe as stdin and one pipe shared by stdout
and stderr. It writes the script as UTF-8 without BOM, closes stdin, and reads the merged output to
end of file. The result is the output, its total size, whether it was truncated, and the exit code.

Every shell takes the script on stdin, so one mechanism serves bash, zsh, PowerShell, a remote shell
through ssh and a shell in a container.

### PowerShell

PowerShell reads stdin line by line by default, which breaks multiline blocks. The bootstrap reads
the raw bytes of stdin, decodes them as UTF-8 and evaluates the whole script at once:

```
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false); $m = New-Object IO.MemoryStream; [Console]::OpenStandardInput().CopyTo($m); Invoke-Expression ([Text.Encoding]::UTF8.GetString($m.ToArray()) + [char]10 + 'if (-not $?) { if ($LASTEXITCODE) { exit $LASTEXITCODE }; exit 1 }')
```

The appended last line returns the exit code of a native program that ends the script, and keeps an
empty script valid. The bootstrap uses single quotes only, so it fits in a double quoted value.

The contract holds on Windows PowerShell 5.1, the floor of KiSS, as measured by `shells.ps1` on
Windows 10 19045 and Windows 11 26100: multiline blocks, here strings, CRLF scripts, UTF-8 in and out
including `Write-Host` and native programs, `exit N`, `throw`, parse errors, stderr merged without
CLIXML, a native program reading stdin to its end, 20000 lines, and a timeout ending the whole tree.

### Processes

- POSIX: `posix_spawn` with `POSIX_SPAWN_SETPGROUP`, the child leads its own process group, and a
  timeout or a cancel sends `SIGKILL` to the group.
- Windows: `CreateProcessW` suspended with `CREATE_NO_WINDOW`, assigned to a Job Object with
  `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, then resumed, so no descendant starts outside the job; a
  timeout or a cancel closes the job.

### Output

- Valid UTF-8 passes unchanged. On Windows, output that is not valid UTF-8 is decoded from the OEM
  code page of the machine (`GetOEMCP`), since some native programs write it whatever the console
  says. Each run of bytes that stays invalid becomes one U+FFFD, so the JSON is always valid and a
  binary output stays close to the size read.
- Past the `output` budget, the server keeps the head, where the command shows what it does, and the
  tail, where builds report their errors, both cut on line ends, and writes between them one line,
  `Long output with N bytes hidden from context`. The pipe is drained to the end either way, so the
  child never blocks on a full pipe.

### Long calls and cancel

A `tools/call` answers as an SSE stream. While the process runs, the server writes an SSE comment
every second: proxies keep the connection open, and the first write that fails means the client is
gone, the user pressed Stop, so the server ends the tree. One mechanism, nothing to track. Both eras
of the SDK abort that stream on a cancel. A client that does not accept SSE gets plain JSON, and the
tree ends once its socket closes.

A reverse proxy notices a client gone at its own pace: through Apache the tree ends a few seconds
after the Stop.

A shell runs 290 seconds at most by default and KiSS waits 310 on a call, as FRONTEND.md sets it:
ten below 300 on one side, ten above on the other. The server ends the run first and the model reads
`Killed after 290 s`, never a timeout of the page; figures that do not fall on 300 say the gap is
meant. A shell given more needs `set mcp timeout` raised past it in KiSS.

### Remote hosts and containers

Since the script travels on stdin, ssh and container engines forward it unchanged, and remoting is
configuration:

```
set shells command gpu ssh -T -o BatchMode=yes root@forge bash -s
set shells command box podman exec -i sandbox bash -s
```

The OpenSSH client ships with Linux, macOS and Windows 10 and later. Two limits hold: the tools on
files work on the machine that runs `server`, and ending a local ssh without a terminal does not end
the remote process, so a remote item wraps its shell in `timeout` on the remote side. A machine that
needs the tools on files or a reliable tree kill runs its own `server`.

## MCP

The server is stateless and speaks both eras on `POST /mcp`:

- 2026-07-28: every request carries its envelope in `_meta`, and the headers `MCP-Protocol-Version`
  and `Mcp-Method` must match the body; `server/discover` answers.
- 2025-11-25: `initialize` answers without `Mcp-Session-Id`. Any other version a client asks for gets
  2025-11-25, as the protocol states.

Notifications get 202, `GET` and `DELETE` get 405. Methods: `tools/list`, `tools/call`,
`resources/list`, `ping`.

The `serverInfo` of either era names the server by its hostname, its title `KiSS Operating System`,
its version, and as its description the line it gives of itself in `show version`:
`server forge 5633ee7 (2026-10-11) on linux`. KiSS lists that line in its own `show version`.

## Proxy routes

The server proxies the servers its `proxy` module names, MCP servers and LLM endpoints alike, each
at `proxy/<name>` of the server, so a page reaches a server it could not: one without CORS, one only
this machine sees, or one whose key stays on the server. The server holds the route, `set proxy url
hf https://router.huggingface.co/v1` and `set proxy key hf`, and the page reaches it by the item of
the same name, `set endpoints proxy hf backend` or `set mcp proxy deepwiki backend`, its own url and
key waiting unused while it goes through; the model may add a route through the config tool, the user letting its url
and typing its key, the form naming where each goes. `resources/list` publishes every route as
`kiss:proxy/<name>`, its `_meta` `kiss/keyed` telling whether it holds a key, so the page checks the
route before it sends an item there. `show proxy` writes after each route where a client reaches it,
`! proxy/hf proxies https://router.huggingface.co/v1`.

The proxy speaks HTTP and knows no protocol: a request to `proxy/<name>/<rest>` goes to the url of
the route then `<rest>`, joined by one slash, its query kept, `proxy/hf/chat/completions` to
`https://router.huggingface.co/v1/chat/completions`. Each request is built anew with its method, its
body and every header but those that stay on its side (`Host`, `Origin`, `Cookie`, `Authorization`,
`x-api-key`, `Accept-Encoding`, the length and the connection), then the headers of the route, which
replace those of the same name and never name the header of its key, then its key in the header
`proxy auth-header` names, `Header` or `Header: Scheme`, a bearer token unless set, `set proxy
auth-header claude x-api-key` for the Messages API of Anthropic; the key of this server never goes
on. The answer comes back with its status and every header but those that stay on its side, each
through the keychain, every secret of the configuration the route was read from read `<removed>`: a
stream of events event by event as it arrives, its data lines joined as a client joins them, an
event the stream leaves open dropped as a client drops it, any other body whole, uncompressed. JSON
goes through yyjson: every string and every name of a property is sealed as decoded, whatever
escapes wrote the key, and the JSON is written again, its numbers as they were written; any other
text, a header and a refusal of the proxy are sealed as they are written. A page served elsewhere
passes CORS: the server lets the headers it asks to send and shows every header it answers. A server
that refuses the key of its route answers 424 with the fix, `set proxy key <name>`, so the page
never takes it for a refusal of its own key nor for a server down for a while, which it asks again;
one that redirects answers 424 naming where, `set proxy url <name>`, since a redirect followed would
carry the key elsewhere; an url no client of the build reaches answers 424 too. A server that cannot
be reached answers 502. A route the configuration does not name answers 404. A server that hides its
own key some other way, deeper than the JSON a page decodes or in pieces across events, betrays a
key it holds, which no filter stops, so a key goes only to a server trusted with it. The client
speaks HTTPS: LibreSSL built with the binary on Windows, the system OpenSSL elsewhere.

## HTTP and security

- `GET /` serves the embedded page, `GET /client.conf` the site configuration, `POST /mcp` the MCP,
  `/proxy/<name>` the servers it proxies, `/conversations` the conversations (see Conversations
  API), behind the same checks.
  The site configuration is `client.conf` of the directory the server runs in, else the one it
  embeds, which names the backend by a relative URL, `set mcp url backend mcp`, so the page reaches
  the server that serves it on any port and behind any proxy or path prefix.
- On a loopback address, `Host` must name loopback and the port, against DNS rebinding.
- A request without `Origin` (a CLI client) or from the origin of the page itself needs no key.
- A request from any other origin needs the key: a page served elsewhere sends it as `mcp key`,
  which KiSS keeps out of the context of the model. Without that rule any site the user visits could
  run commands through `fetch`.
- Listening off loopback requires `server key`; the server refuses to start without it. The key is
  compared in constant time.
- `/mcp` answers CORS preflights and allows `Authorization`, `Content-Type`, `MCP-Protocol-Version`
  and `Mcp-Method`.

The server listens without TLS: a LAN or the internet reaches it through a reverse proxy or an ssh
tunnel.

## Delivery

Prebuilt static binaries: Linux built against musl, macOS with no dependency outside the system,
Windows with the static runtime (`/MT`). Each archive holds `server` and nothing else; the page and
the factory configuration live inside it.

## Batches

Each batch ends with a test from end to end on a Linux machine, a KiSS page running a command
through the binary.

1. Skeleton: the engine with the `server` module, `server.conf`, HTTP, page, security, MCP of both
   eras, the config tool, the console and the log.
2. Process layer on POSIX and the shell tools, output repair and truncation, SSE and cancel.
3. The view tool.
4. Windows: process layer, OEM decoding, the PowerShell bootstrap, checked on Windows 10 and 11.
5. Static builds of the three platforms.
6. The conversations, a file each, the agent of the server, and the pages that follow them.
