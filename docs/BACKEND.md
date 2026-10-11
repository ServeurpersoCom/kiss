# KiSS server

The reference of the backend: one binary, `server`, that runs commands and views files on the machine
it runs on, for KiSS or any other MCP client.

## Commands

```
commands
  [linux|macos|windows] set <module> <key> [item] <value>
  [linux|macos|windows] no <module> <key> [item]
  [linux|macos|windows] no <module> <item>
  show <module> [key] [item]
  show <module> <item>
  show running-config [all]
  show startup-config
  show version
  show logging
  copy running-config startup-config
  copy startup-config running-config
  write [memory]
  <command> | begin|count|exclude|include <pattern>
  exit, quit                        the console only: stop the server

  [linux|macos|windows]   platform override: the line holds on that platform only
                          and wins over the same line without it

modules
  server
    host <address>                  127.0.0.1
    port <1..65535>                 8089
    hostname <word>                 the host name
    key <secret>                    required off loopback
    cwd <path>                      the start directory
    log errors|informational|debugging
                                    debugging
    buffered <bytes>                65536, the log show logging keeps
  shells
    command <item> <line>           linux bash, macos zsh bash, windows powershell, pwsh
    timeout <item> <seconds>        290
    output <item> <bytes>           65536
  view
    output <bytes>                  65536
    image <bytes>                   52428800, an image file read at most
    pixels <count>                  100000000, decoded at most
    side <pixels>                   1920, the longest side sent
    quality <1..100>                85, of a JPEG made again

tools
  <hostname>_config                 description lines
  <hostname>_<shell>                description command
  <hostname>_view                   description path [view_range]
```

## The server

`server` is an MCP server over Streamable HTTP and a process launcher. One runs on each machine that
works for the model: a GPU server, a Windows box, a laptop. A KiSS page drives as many as its
`kiss.conf` names, each under its own name, so the page never changes when a machine joins.

The C++ holds mechanisms only: HTTP, MCP, the process layer of each OS, decoding and truncation of
output, the security checks. Every behavior is data in `server.conf`: which shells exist, how each
one starts, its limits, the working directory. A new shell, a remote host or a container is a line
of configuration, never a rebuild.

The binary also serves the KiSS page, so one download gives a working harness: start `server`, open
`http://127.0.0.1:8089/`.

## The repository

Front and back are one project and peers, `webui/` and `server/`, laid out alike: the same
`engine/`, `commands/`, `modules/` and `tools/` on both sides, a file per concept. A feature adds a
file; no file grows with the features.

```
webui/                   the page, as FRONTEND.md describes it
webui/public/kiss.conf   the site configuration the page and the server serve by default
server/main.cpp          the start: defaults, startup-config, console, listener
server/engine/           the CLI: text, parse, filter, near, schema, values, registry, archive, run
server/commands/         one file per command, registered by existing
server/modules/          one file per module: its keys, rules, how it applies to the server
server/tools/            one file per tool source, registered by existing
server/mcp/              the MCP: the entry of both eras, the JSON-RPC answers, the methods
server/listener.cpp|h    the socket, moved at once when server host or server port changes
server/http.cpp|h        the routes and the checks of Host, Origin and the key
server/console.cpp|h     the console of the interactive mode, opened on a terminal in the foreground
server/term.h            the terminal of the console, per OS: raw mode, a key, the width
server/log.cpp|h         the log: [Module] Line
server/job.cpp|h         spawn, pipes, tree kill, per OS
server/output.cpp|h      UTF-8 repair and head plus tail truncation
server/image.cpp|h       an image as a model is sent it: sized, turned, resized, made a JPEG
server/utf8.h            the UTF-8 boundary of Windows: argv, fopen
server/server.conf       the defaults the binary embeds
vendor/                  cpp-httplib, yyjson, stb
tools/public/            index.html.gz, the page the binary embeds
tools/xxd.cmake          embeds a file as a C array
tools/version.cmake      the git commit of the build
CMakeLists.txt           the server, built without npm, into build/server
buildwebui.sh            builds the page in webui/ and gzips it into tools/public/, optional, first
build.sh                 builds the server with CMake alone
build.cmd                the same on Windows, with Visual Studio Build Tools and Ninja
format.sh                clang-format over the C++, vendors excluded
```

Every file has one source: the binary embeds `webui/public/kiss.conf` and `server/server.conf` as
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
the item name next, and the value comes last. Each word may be any prefix that names one word only,
the way Cisco IOS reads a line, the value of an enum too: `set server log inf` sets
`informational`, and the configuration keeps the whole word.

A batch compiles every line before the first one runs, runs them on a draft of the running
configuration, checks the rules of every module on the draft, then replaces the running
configuration and applies the modules whose keys changed. A failure at any step applies nothing.

The commands are those of the page that fit a server: `set`, `no`, `show <module>`,
`show running-config [all]`, `show startup-config`, `show version`, `copy` between the
running-config and the startup-config, and `write memory`, or `write` alone, IOS's way to save; and
one of its own, `show logging`. A line takes a filter after a pipe, `begin`, `count`,
`exclude` or `include`, as IOS does. A secret never prints: `show` writes `! server key is set`.

Two roles send batches. The administrator writes `server.conf` and the command line. An MCP client
runs the config tool, whose lines a model may write, so it writes no secret: a secret would cross the
context of the model.

A module registers itself by existing, with its keys, its rules and what it applies: `server`
rebinds its socket, `shells` rebuilds the tool list, `view` bounds what its tool returns. The engine
knows no module by name, so a module joins without a change to the engine.

### server.conf

`server.conf` holds set lines in the grammar of `kiss.conf`: a module, a key, the item name for a
collection, then the value. `!` starts a comment, `no` removes an item or a value.

The startup-config is `server.conf` of the directory the server runs in: the root of the repository
during development, where git ignores it, the working directory of a systemd unit, the directory of
a prebuilt binary. Several servers run on one machine from several directories. At start the file
runs as a batch of the administrator, and a line it refuses stops the server with the reason. The
result is the running configuration, in memory; `copy running-config startup-config` writes it
back to the file whole.

A value resolves through layers, the first that holds it winning:

1. the running configuration;
2. `server/server.conf`, embedded in the binary, as `kiss.conf` sits below every save of the page;
3. the fallback of the key: the host name of the machine for `server hostname`, the start directory
   for `server cwd`.

### Platform overrides

One `server.conf` serves every machine: it is copied unchanged to `forge`, `studio` and `nomad`, and
each reads the lines that hold for it. A `set` or `no` line may start with `linux`, `macos` or
`windows`; it then holds on that platform only, and wins over the same line without a prefix. Lines
without a prefix hold everywhere, and prefixes appear only where platforms truly differ: paths, and
shells that exist on one OS only.

```
set server port 8089
set server cwd /srv/work
windows set server cwd D:\work
macos set server cwd /Users/me/work
linux set shells timeout bash 600
```

All three listen on 8089. `forge`, on Linux, works in `/srv/work` and gives its builds ten minutes;
`studio`, on Windows, works in `D:\work`; `nomad`, on macOS, in `/Users/me/work`.

Each machine keeps the lines of the other platforms as they are and writes them back with its own,
so a `copy running-config startup-config` on Linux leaves the Windows lines of the file untouched.
A line without a prefix on a key this platform holds changes it for this platform.

An item whose program is not found on the PATH stays out, with one line in the log. The default
configuration therefore lists every candidate shell and each machine serves the ones it has.

### Modules

| key                     | kind   | meaning                                                         |
| ----------------------- | ------ | --------------------------------------------------------------- |
| `server host`           | string | address to listen on                                            |
| `server port`           | number | port to listen on                                               |
| `server hostname`       | string | the prompt and the prefix of every tool name, as IOS's hostname |
| `server key`            | secret | bearer key, required off loopback                               |
| `server cwd`            | string | working directory of every tool, the start directory by default |
| `server log`            | enum   | `errors`, `informational` or `debugging`, what the log tells    |
| `server buffered`       | number | bytes of the last lines of the log `show logging` keeps         |
| `shells command <item>` | string | command line that runs a script read on stdin                   |
| `shells timeout <item>` | number | seconds a call may run                                          |
| `shells output <item>`  | number | bytes of output a call returns                                  |
| `view output`           | number | bytes of text `view` returns, its head and its tail             |
| `view image`            | number | bytes of an image file `view` reads at most                     |
| `view pixels`           | number | pixels an image may declare before it is decoded                |
| `view side`             | number | the longest side of an image sent, in pixels                    |
| `view quality`          | number | the JPEG quality of an image made again, 1 to 100               |

A command line splits into arguments on spaces, double quotes grouping. On Windows it reaches
`CreateProcessW` unchanged, which is the command line Windows expects.

### The defaults

```
! server.conf: the defaults the KiSS server embeds
set server host 127.0.0.1
set server port 8089
set server log debugging
set server buffered 65536
linux set shells command bash bash -s
linux set shells timeout bash 290
linux set shells output bash 65536
macos set shells command zsh zsh -s
macos set shells command bash bash -s
windows set shells command powershell powershell -NoProfile -NonInteractive -Command "<boot>"
set shells command pwsh pwsh -NoProfile -NonInteractive -Command "<boot>"
set view output 65536
set view image 52428800
set view pixels 100000000
set view side 1920
set view quality 85
```

Every shell carries its own `timeout`, 290 seconds, and `output`, 65536 bytes, as `bash` shows.
`<boot>` stands for the PowerShell bootstrap below, written in full in the file.

Each OS gets a default that works: bash on Linux, zsh and bash on macOS, Windows PowerShell 5.1 on
Windows, and PowerShell 7 wherever it is installed. A shell the default leaves out, Git Bash or the
bash of WSL, is one more line.

## Running

`server` takes no argument. Its stdin tells how it runs: a terminal it runs in the foreground of
opens the console; anything else, `/dev/null` under systemd, a Windows service, `nohup`,
`< /dev/null` or a start with `&` from a shell, runs it as a daemon that leaves stdin alone.

Its log goes to stderr, one line per event in the form of every GGML project of the house,
`[Module] Line`, and opens on the version. A server tells everything that happens, as the console
of a router does by default: `server log` sets how much, at once, no restart.

- `errors`: what fails or is refused, a request without its key, a call refused or killed, a
  listener that cannot bind.
- `informational`, and the above: the events, a client that connects, each call with its why as it
  starts and its outcome with its duration as it ends, each change of the configuration.
- `debugging`, the default, and the above: the detail, each request with its method, address and
  era, every other argument of a call, a text by its first line, and the bytes a call sends back.

`show logging` prints the last lines of the log, as IOS keeps them with `logging buffered`:
`server buffered` sets how many bytes stay, every line of the start included, so the console, KiSS
through the config tool, and the model asked to diagnose read what happened without the terminal of
the machine.

```
[Server] KiSS Operating System, server 5633ee7 (2026-10-11) on linux
[Server] Listening on http://127.0.0.1:8089
[MCP] server/discover from 192.168.1.20, 2026-07-28
[MCP] Client kiss 5633ee7 connects from 192.168.1.20
[MCP] tools/call from 192.168.1.20, 2026-07-28
[MCP] Call forge_bash: Builds the server
[MCP] Call forge_bash: command cd kiss && ./build.sh (2 lines)
[MCP] Call forge_bash: exit 0 in 5321 ms
[MCP] Call forge_bash: 1838 bytes back by SSE
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
forge# set server port 9000
[Engine] Configured by the administrator
- set server port 8089
+ set server port 9000
forge# copy run start
! copied running-config to startup-config
```

The line is edited as in a shell, and completes as IOS does, from the words the engine reads:

- Tab completes the word when one word fits, else the start every fitting word shares, else lists
  them; `?` lists what may follow, or the words the word typed starts, and leaves the line as it is.
  Ctrl-V types the next key as it is, a `?` in a value.
- Left and Right, Home and End or Ctrl-A and Ctrl-E move; Backspace and Delete erase a character,
  Ctrl-U the line before the cursor, Ctrl-K the line after it; Up and Down walk the history.
- `exit` and `quit`, Ctrl-C, and Ctrl-D on an empty line stop the server. They are words of the
  console, not of the CLI, so no MCP client can stop it.

## Tools

A tool name is `<hostname>_<item>`: `forge_bash`, `studio_powershell`, `nomad_zsh`. The hostname
is by default the one of the machine, lowercased with every character outside `[a-z0-9]` made `_`,
which keeps the name valid for every model API. Two machines never collide, and KiSS filters a tool by its name with
`set tools use <tool> off`.

A description is short: what the tool does and the machine it acts on, `Run a bash command on
forge`. A cut output, a timeout or a resized image, the result tells. Only the hostname changes it,
so the tool list stays the same from turn to turn and the prompt cache of the model holds.

Every tool takes `description` first, why the call is made, then what it does: its schema lists it
first and says so, for a model to write the why before the what. A model still writes its arguments
in the order it likes, so KiSS shows `description` on a folded call by its name, wherever it stands:
`display preview description` is its default, which `tools preview <tool>` overrides for one tool.
The schema of a tool is its law: a key outside it, a required key missing or a required
string empty refuses the call before it runs, with `Unknown argument <key>` or
`<Key> argument required`, so a misnamed argument never runs as if it were absent.

A result is plain text in sentences, no prompt and no `!`: the CLI is the config tool alone. The
model wrote the call and keeps it in its context, so a result never echoes it.

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

`<name>_view` takes `description`, `path`, relative to `server cwd` or absolute, and `view_range`,
the lines `[start, end]` of a text file, from 1, `end` -1 for the last. It is the one file tool:
every OS creates and edits files through its shell, while `view` gives the model what a shell gives
badly, an image it sees and a text it can cite by line. With `view` on and every other tool off in
KiSS, a machine is open to the model for reading only.

- A directory lists every entry two levels deep, sorted by path, each with the size of all it
  holds, as `find` and `du -sh` would: hidden entries and the content of `node_modules` stay out.
  The last line counts the entries.
- A PNG, JPEG, GIF or WebP image, known by its first bytes, comes back as an image block. A JPEG
  that fits `view side` goes as it is, and an opaque PNG that fits too when it weighs less than its
  JPEG, as a drawing or a screenshot does. Any other image is made a JPEG of `view quality` that
  fits it: resized in linear light with the same scale on both sides, turned as its EXIF says, a
  transparent one laid on white. A text block before it tells the image read and the image sent,
  `Image image/png 6000x4000, 47087504 bytes, sent as image/jpeg 1920x1280, 292632 bytes`, so the
  model knows what it sees. A WebP goes as it is when it fits, and is refused when it does not.
- Before anything decodes it, an image is refused past `view image` bytes, or when its header
  declares more than `view pixels` pixels, so neither a huge file nor a small one that inflates
  holds the memory of the machine. A photo of 50 MB reaches the model in a few hundred KB, well
  within the 5 MiB a request may weigh on the Hugging Face router.
- A text file comes back with numbered lines, `N<tab>line`, then `300 lines`, or
  `Lines 10-20 of 300` for a range. Past `view output`, the head and the tail stay, as for a shell.
  The file streams through, so its size costs time and never memory.
- Another binary file, one with a NUL byte near its start, is refused with `Binary file`.

### The config tool

`<name>_config` takes `description` and `lines`, runs the lines as a batch of CLI on the engine and
answers with its output and its change, as the `config` tool of KiSS does. KiSS configures every
backend from the page through it, under the consent firewall when the model calls it, and any other
MCP client gets the same CLI. A change of `server host` or `server port` moves the listener at once,
once the address is checked free.

A secret never crosses the context of the model. It reaches the server three ways: `server.conf`,
the console, and MCP elicitation: a `set` line of a secret without its value answers with a request
for input in url mode, which KiSS opens in the secure window of its own secrets, and the value goes
from the user to the server alone. The model reads that the user typed it. A line that writes the
value of a secret itself is refused, since a model wrote it. The same elicitation, in form mode,
carries the questions and confirmations the server asks the user.

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

The OpenSSH client ships with Linux, macOS and Windows 10 and later. Two limits hold: `view` works on
the machine that runs `server`, and ending a local ssh without a terminal does not end the remote
process, so a remote item wraps its shell in `timeout` on the remote side. A machine that needs `view`
or a reliable tree kill runs its own `server`.

## MCP

The server is stateless and speaks both eras on `POST /mcp`:

- 2026-07-28: every request carries its envelope in `_meta`, and the headers `MCP-Protocol-Version`
  and `Mcp-Method` must match the body; `server/discover` answers.
- 2025-11-25: `initialize` answers without `Mcp-Session-Id`. Any other version a client asks for gets
  2025-11-25, as the protocol states.

Notifications get 202, `GET` and `DELETE` get 405. Methods: `tools/list`, `tools/call`, `ping`.

The `serverInfo` of either era names the server by its hostname, its title `KiSS Operating System`,
its version, and as its description the line it gives of itself in `show version`:
`server forge 5633ee7 (2026-10-11) on linux`. KiSS lists that line in its own `show version`.

## HTTP and security

- `GET /` serves the embedded page, `GET /kiss.conf` the site configuration, `POST /mcp` the MCP.
  The site configuration is `kiss.conf` of the directory the server runs in, else the one it
  embeds, which names the backend by a relative URL, `set mcp url local mcp`, so the page reaches
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

TLS stays out of the binary: a LAN or the internet reaches `server` through a reverse proxy or an
ssh tunnel.

## Delivery

Prebuilt static binaries: Linux built against musl, macOS with no dependency outside the system,
Windows with the static runtime (`/MT`). Each archive holds `server` and nothing else; the page and
the default configuration live inside it.

## Batches

Each batch ends with a test from end to end on a Linux machine, a KiSS page running a command
through the binary.

1. Skeleton: the engine with the `server` module, `server.conf` with layers and prefixes, HTTP,
   page, security, MCP of both eras, the config tool, the console and the log.
2. Process layer on POSIX and the shell tools, output repair and truncation, SSE and cancel.
3. The view tool.
4. Windows: process layer, OEM decoding, the PowerShell bootstrap, checked on Windows 10 and 11.
5. Static builds of the three platforms.
6. The storage tool for conversations and configuration, called by KiSS and never by the model.

Then the server grows into a router by modules alone, the engine unchanged:

- `mcp`: a gateway that mounts remote MCP servers, renames and filters their tools, and keeps their
  keys on the server, so no browser ever holds them.
- `endpoints`: a router for OpenAI and Anthropic compatible endpoints, keys held on the server, for
  several users.
