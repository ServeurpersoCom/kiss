# KiSS server

The reference of the backend: one binary, `server`, that runs commands and edits files on the machine
it runs on, for KiSS or any other MCP client.

## The server

`server` is an MCP server over Streamable HTTP and a process launcher. One runs on each machine that
works for the model: a laptop, a pod, a VM, a Windows box. A KiSS page drives as many as its
`kiss.conf` names, each under its own name, so the page never changes when a machine joins.

The C++ holds mechanisms only: HTTP, MCP, the process layer of each OS, decoding and truncation of
output, the security checks. Every behavior is data in `server.conf`: which shells exist, how each
one starts, its limits, the working directory. A new shell, a remote host or a container is a line
of configuration, never a rebuild.

The binary also serves the KiSS page, so one download gives a working harness: start `server`, open
`http://127.0.0.1:8089/`.

## The repository

Front and back are one project and peers:

```
src/ ...                 the page, as described in ARCHITECTURE.md
server/CMakeLists.txt    the server, built without npm
server/server.cpp        main: command line, startup-config, HTTP
server/engine/           the CLI: parse, schema, values, run, registry, as in the page
server/modules/          one file per module: its keys, rules, how it applies to the server
server/mcp.cpp|h         the MCP methods of both eras, stateless
server/tools.cpp|h       the tools: one per shell item, view, str_replace, create_file, config
server/process.cpp|h     spawn, pipes, tree kill, per OS
server/output.cpp|h      UTF-8 repair and head plus tail truncation
server/public/           index.html.gz, versioned, and kiss.conf
server/vendor/           cpp-httplib, yyjson
server/xxd.cmake         embeds the page and the default server.conf
CMakeLists.txt           add_subdirectory(server)
buildwebui.sh            builds the page and gzips it into server/public/, optional and run first
build.sh                 builds the server with CMake alone
format.sh                clang-format over the C++, vendors excluded
```

`server/public/index.html.gz` is versioned so a C++ developer builds the server with CMake alone.

There is no process library in the vendors. sheredom/subprocess.h, which llama.cpp uses, offers no
process group on POSIX and no Job Object nor suspended start on Windows, so it kills the direct child
only and grandchildren outlive a timeout. `process.cpp` owns that layer.

## Configuration

### The engine

The configuration runs on the engine of the page, rewritten in C++: the same words, the same
batches, the same laws. A key is two fixed words, a module then a key; a key of a collection takes
the item name next, and the value comes last. Each word may be any prefix that names one word only,
the way Cisco IOS reads a line.

A batch compiles every line before the first one runs, runs them on a draft of the running
configuration, checks the rules of every module on the draft, then replaces the running
configuration and applies the modules whose keys changed. A failure at any step applies nothing.

The running configuration lives in memory; the startup-config is `server.conf`, and `write` saves
the running configuration there. A secret never prints: `show` writes `<removed>` in its place.

A module registers itself by existing, with its keys, its rules and what it applies: `server`
rebinds its socket, `shells` rebuilds the tool list, `files` turns its tools on or off. The engine
knows no module by name, so a module joins without a change to the engine.

### server.conf

`server.conf` holds set lines in the grammar of `kiss.conf`: a module, a key, the item name for a
collection, then the value. `!` starts a comment, `no` removes an item or a value.

A value resolves through layers, the first that holds it winning:

1. the set lines given on the command line, `server -s "set server port 9000"`;
2. the file named by `server -c path`, or `server.conf` next to the binary when it exists;
3. the default `server.conf` embedded in the binary;
4. the default of the key in the schema.

A `no` line in a higher layer removes an item a lower layer defines.

### One file for every machine

A line may start with `linux`, `macos` or `windows`; it then holds on that platform only, and it wins
over the same line without a prefix. Lines without a prefix hold everywhere. The same `server.conf`
is copied unchanged to every machine, and prefixes appear only where platforms truly differ: paths,
and shells that exist on one OS only.

An item whose program is not found on the PATH stays out, with one line in the log. The default
configuration therefore lists every candidate shell and each machine serves the ones it has.

`server --show` prints the running configuration of this machine with the prefixes resolved and the
secret values replaced by `<removed>`.

### Modules

| key                     | kind   | meaning                                                         |
| ----------------------- | ------ | --------------------------------------------------------------- |
| `server host`           | string | address to listen on                                            |
| `server port`           | number | port to listen on                                               |
| `server name`           | string | prefix of every tool name, the host name by default             |
| `server key`            | secret | bearer key, required off loopback                               |
| `server cwd`            | string | working directory of every tool, the start directory by default |
| `shells command <item>` | string | command line that runs a script read on stdin                   |
| `shells timeout <item>` | number | seconds a call may run, and the bound of the `timeout` argument |
| `shells output <item>`  | number | bytes of output a call returns                                  |
| `files use`             | enum   | `on` or `off`, the three file tools                             |
| `files read`            | number | bytes `view` returns at most                                    |

A command line splits into arguments on spaces, double quotes grouping. On Windows it reaches
`CreateProcessW` unchanged, which is the command line Windows expects.

### The default server.conf

```
! server.conf: defaults of the KiSS server, set lines only
set server host 127.0.0.1
set server port 8089
linux set shells command bash bash -s
macos set shells command zsh zsh -s
macos set shells command bash bash -s
windows set shells command powershell powershell -NoProfile -NonInteractive -Command <boot>
set shells command pwsh pwsh -NoProfile -NonInteractive -Command <boot>
set files use on
```

`<boot>` stands for the PowerShell bootstrap below, written in full in the file.

Each OS gets a default that works: bash on Linux, zsh and bash on macOS, Windows PowerShell 5.1 on
Windows, and PowerShell 7 wherever it is installed. A shell the default leaves out, Git Bash or the
bash of WSL, is one more line.

## Tools

A tool name is `<server name>_<item>`: `pod_bash`, `monstre_powershell`, `pod_view`. The server name
is the host name lowercased with every character outside `[a-z0-9]` made `_`, which keeps the name
valid for every model API. Two machines never collide, and KiSS filters a tool by its name with
`set tools use <tool> off`.

A description holds the OS, the shell, its command line and the working directory. It is fixed for
the life of the process, so the tool list stays the same from turn to turn and the prompt cache of
the model holds.

### Shell tools

One tool per shell item that starts. Its arguments are `script`, the script to run, and `timeout`,
seconds bounded by the item.

### File tools

- `view`: a text file with numbered lines and an optional line range, a directory two levels deep,
  or an image returned as an image block.
- `str_replace`: replaces one exact string, which must occur exactly once.
- `create_file`: writes a file, creating its parent directories.

Paths are UTF-8, relative to `server cwd` or absolute.

### The config tool

`<name>_config` runs a batch of CLI lines on the engine and answers with its output and its change,
as the `config` tool of KiSS does. KiSS configures every backend from the page through it, under the
consent firewall when the model calls it, and any other MCP client gets the same CLI.

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
  says. What stays invalid becomes U+FFFD, so the JSON is always valid.
- Past the `output` budget, the server keeps the head, where the command shows what it does, and the
  tail, where builds report their errors, and writes between them one line with the number of bytes
  left out. The pipe is drained to the end either way, so the child never blocks on a full pipe.

### Long calls and cancel

A `tools/call` answers as an SSE stream. While the process runs, the server writes an SSE comment
every second: proxies keep the connection open, and the first write that fails means the client is
gone, the user pressed Stop, so the server ends the tree. One mechanism, nothing to track.

### Remote hosts and containers

Since the script travels on stdin, ssh and container engines forward it unchanged, and remoting is
configuration:

```
set shells command gpu ssh -T -o BatchMode=yes root@pod bash -s
set shells command box podman exec -i sandbox bash -s
```

The OpenSSH client ships with Linux, macOS and Windows 10 and later. Two limits hold: the file tools
work on the machine that runs `server`, and ending a local ssh without a terminal does not end the
remote process, so a remote item wraps its shell in `timeout` on the remote side. A machine that needs
file tools or a reliable tree kill runs its own `server`.

## MCP

The server is stateless and speaks both eras on `POST /mcp`:

- 2026-07-28: every request carries its envelope in `_meta`, and the headers `MCP-Protocol-Version`
  and `Mcp-Method` must match the body; `server/discover` answers.
- 2025-11-25: `initialize` answers without `Mcp-Session-Id`. Any other version a client asks for gets
  2025-11-25, as the protocol states.

Notifications get 202, `GET` and `DELETE` get 405. Methods: `tools/list`, `tools/call`, `ping`.

## HTTP and security

- `GET /` serves the embedded page, `GET /kiss.conf` the site configuration, `POST /mcp` the MCP.
  The site configuration names the backend by a relative URL, `set mcp url local /mcp`, so the page
  reaches the server that serves it on any port and behind any proxy or path prefix.
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

Each batch ends with a test from end to end on the pod, a KiSS page running a command through the
binary.

1. Skeleton: the engine with the `server` module, `server.conf` with layers and prefixes, HTTP,
   page, security, MCP of both eras, the config tool, `server --show`.
2. Process layer on POSIX and the shell tools, output repair and truncation, SSE and cancel.
3. File tools.
4. Windows: process layer, OEM decoding, the PowerShell bootstrap, checked on Windows 10 and 11.
5. Static builds of the three platforms.
6. The storage tool for conversations and configuration, called by KiSS and never by the model.

Then the server grows into a router by modules alone, the engine unchanged:

- `mcp`: a gateway that mounts remote MCP servers, renames and filters their tools, and keeps their
  keys on the server, so no browser ever holds them.
- `endpoints`: a router for OpenAI and Anthropic compatible endpoints, keys held on the server, for
  several users.
