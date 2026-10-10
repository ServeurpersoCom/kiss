# KiSS

**The chat UI with no settings panel. You talk; the model sets up the page.**

Point KiSS at any OpenAI compatible endpoint and just say what you want: use the Qwen on my box,
plug in my MCP sandbox, go light with a purple accent. The model types the configuration itself, in
a real CLI modeled on Cisco IOS, and anything that widens its reach waits for your yes.

One HTML file. No backend, no account, no settings page. Ever.

## You talk, it configures itself

```
you     use the qwen on http://pod:8080/v1 and go light

KiSS    set endpoints url pod http://pod:8080/v1
        set chat model pod/qwen3:8b
        set style bg #faf9f5
        set style fg #141413

        Allow this change?
        + set chat model pod/qwen3:8b
        + set endpoints url pod http://pod:8080/v1
        [ Once ]  [ Always ]  [ Refuse ]
```

Endpoints, models, sampling, system prompt, MCP servers, tools, display, style: the model reaches
all of it through one tool, and so do you, after a `/`.

## A real CLI, the Cisco IOS way

- Any unambiguous prefix works, `sh run`; an ambiguous one says so, the way IOS does.
- `show running`, `show diff`, named saves, `| include`, `| count`, tab completion.
- A batch applies whole or not at all, and answers with the exact change, `-` then `+`.

```
/sh run | include endpoints
/show diff work session
/save work
/load home
```

## Secure by design

The model configures the page, so KiSS guards what it can reach.

- **The firewall reads the effect, not the command.** `set`, `no`, `reset` or `load`: whatever
  spells a change, the resolved diff decides.
- **Privilege per module**, `deny`, `ask` or `allow`; the model changes it only on your yes,
  every time.
- **Nothing leaks through the page.** No image in a reply and no style ever loads from another
  host, so nothing rides out on them.
- **Every MCP tool starts in consent.** Each call shows its real arguments: once, always, or
  refuse.
- **Secrets never pass through the model.** It leaves the value out; a masked field asks you.
- **Stop means stop.** Nothing is ever half applied.

## Everything a chat should do, done right

- **Edit or retry any message** and a new branch opens beside it, the old one kept, arrows to go
  between: the model restarts from the exact prefix, so your server reuses its KV cache.
- **Copy any message** as its source, close any command you typed; export and import one
  conversation or all of them, by asking: `/export all`.
- **MCP over Streamable HTTP** with the official SDK, 2025 and 2026 protocols alike.
- **Per model parameters**, sent only when set, under their OpenAI names.
- **Rendering that holds up**: Markdown, LaTeX, code highlighted in 192 languages, for the
  thinking as for the reply, or either plain in monospace.
- **Restyle anything**, just by asking: every token of the page is a key, `set style bg #faf9f5`,
  and named CSS sheets go over it.

## Quick start

```
./build.sh
npm run preview
```

The page serves on http://localhost:4173. To host it, copy `dist/index.html` to any static web
server, Apache or nginx, with an optional `kiss.conf` beside it giving a fresh page its LLM: any
OpenAI compatible endpoint, a llama-server as well.

```
set endpoints url prod http://localhost:8080/v1
```

## Learn more

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the engine, the grammar of the CLI, the firewall,
and every law the tests hold.

## License

MIT
