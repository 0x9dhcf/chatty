# chatty

![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)
![build](https://github.com/0x9dhcf/chatty/actions/workflows/ci.yml/badge.svg)
![platform](https://img.shields.io/badge/platform-Linux-informational.svg?logo=linux&logoColor=white)
![license](https://img.shields.io/badge/license-MIT-green.svg)

A terminal assistant powered by LLMs. Supports multiple providers (OpenAI, Anthropic, Gemini, Mistral) with tool use for shell commands, file I/O, and background processes.

- Persistent named sessions with per-session configuration (provider, model, thinking effort)
- Markdown rendering in terminal via [mdtty](https://github.com/0x9dhcf/mdtty)
- Interactive line editing with history and tab completion via [promptty](https://github.com/0x9dhcf/promptty)

Built on [agt](https://github.com/0x9dhcf/agt).

## Build

Requires CMake 3.21+, Ninja, and a C++23 compiler.

System dependencies: `libcurl`, `libsqlite3`.

```bash
# Debug (with sanitizers)
cmake --preset debug
cmake --build --preset debug

# Release (with LTO)
cmake --preset release
cmake --build --preset release
```

For local agt development, point CPM to your local source:

```bash
cmake --preset debug -DCPM_agt_SOURCE=/path/to/agt
```

## Usage

Set at least one provider API key:

```bash
export OPENAI_API_KEY=...
export ANTHROPIC_API_KEY=...
export GEMINI_API_KEY=...
export MISTRAL_API_KEY=...
```

Then run:

```bash
./build/debug/chatty
```

## Commands

| Command              | Description                                  |
|----------------------|----------------------------------------------|
| `/provider [name]`   | Switch LLM provider                          |
| `/model [name]`      | Switch model                                 |
| `/thinking [level]`  | Set thinking effort (none/low/medium/high)   |
| `/environment`       | Show available providers and models           |
| `/new`               | Start a new session                           |
| `/resume`            | Resume a saved session                        |
| `/delete`            | Delete current session and start new          |
| `/help`              | Show available commands                       |
| `Ctrl-D`             | Quit                                          |

## Sessions

Conversation history is persisted automatically. A session is created and named from your first message. Each session stores its own provider, model, and thinking effort settings.

Sessions are stored as SQLite databases under `~/.local/state/chatty/sessions/`. Line editor history is kept in `~/.local/state/chatty/history`.

## Tools

- **shell**: execute a command and return its output
- **spawn**: run a process in the background
- **file_read**: read file contents (support partial reads)
- **file_write**: write to a file (supports partial writes)
- **ask**: present interactive choices to the user
- **web_search**: query the web via [Tavily](https://tavily.com) and return a synthesized answer plus top results (registered only when `TAVILY_API_KEY` is set)
- **web_extract**: fetch one or more URLs via Tavily and return their cleaned content as markdown (registered only when `TAVILY_API_KEY` is set)

To enable the web tools, set:

```bash
export TAVILY_API_KEY=...
```

When both web tools are registered, the system prompt instructs the model to treat them as paid external calls and use them sparingly: prefer `web_search` first, only `web_extract` on a specific URL or when search snippets were insufficient, and default to `extract_depth=basic`.

## License

MIT. See [LICENSE](LICENSE).
