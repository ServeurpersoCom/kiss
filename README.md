# KiSS

**The chat UI with no settings panel. You talk; the model sets up the page.**

One HTML file, no backend, no account. Give it one endpoint, yourself or through the `kiss.conf` of
the site, and the model bootstraps the rest: other endpoints, MCP servers, tools, style, typed by
the model itself in a CLI modeled on Cisco IOS. Anything that opens a way out of the page waits for
your yes, and a key goes from your keyboard to the page, never through the model.

```
you     add Hugging Face, I have a token, and go light

KiSS    set endpoints url hf https://router.huggingface.co/v1
        set endpoints key hf
        set style bg #faf9f5
        set style fg #141413

        Value of endpoints key hf
        [ ************ ]  [ OK ]  [ Cancel ]

        Allow this change?
        + set endpoints url hf https://router.huggingface.co/v1
        [ Once ]  [ Refuse ]

        ! the user typed endpoints key hf
        + ! endpoints key hf is set
        + set endpoints url hf https://router.huggingface.co/v1
        - set style bg "oklch(0.17 0.005 260)"
        + set style bg #faf9f5
        - set style fg #f2f1ef
        + set style fg #141413
```

## Endpoints

KiSS speaks the OpenAI compatible chat completions for now, plus the Anthropic Messages API and the
OpenAI Responses API in their own protocols. Type these lines after a `/`, or just ask the model. A
key left out opens a masked field: it never enters the conversation.

**llama-server**, or any OpenAI compatible server:

```
set endpoints url local http://localhost:9931/v1
```

**Hugging Face**:

```
set endpoints url hf https://router.huggingface.co/v1
set endpoints key hf
```

**Claude**:

```
set endpoints url claude https://api.anthropic.com/v1
set endpoints protocol claude messages
set endpoints key claude
```

**ChatGPT**:

```
set endpoints url gpt https://api.openai.com/v1
set endpoints protocol gpt responses
set endpoints key gpt
```

Then `show models`, `set chat model claude/<model>`, and `copy run start` to keep it.

## A real CLI, the Cisco IOS way

```
sh run | include ^set endpoints
show running-config all | count ^set tools
show diff work running-config
copy running-config work
copy run start
```

Any unambiguous prefix works. A batch applies whole or not at all and answers with the exact
change, `-` then `+`. Endpoints, models, sampling, system prompt, MCP servers, tools, display and
style all go through it, for the model as for you.

## Secure by design

- **A way out asks every time**: a new endpoint or MCP address, a tool opened. No answer turns the
  question off, and a key never follows a URL the model wrote.
- **The firewall reads the effect**, not the command: whatever spells a change, the diff decides.
- **Every MCP tool starts in consent**, each call shown with its real arguments.

## Quick start

```
./build.sh
npm run preview
```

To host it, copy `dist/index.html` to any static web server, with an optional `kiss.conf` beside
it: `set` lines that give a fresh page its endpoints.

## Built with itself

KiSS is developed with KiSS, by a variety of models, Claude, GPT, Qwen, GLM and more, working in a
rootless container they reach through MCP.

[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): the engine, the grammar, the firewall, and every law
the tests hold.

## License

MIT
