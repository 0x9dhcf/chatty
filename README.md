# chatty

[![CI](https://github.com/0x9dhcf/chatty/actions/workflows/ci.yml/badge.svg)](https://github.com/0x9dhcf/chatty/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A terminal assistant powered by LLMs. Supports multiple providers (OpenAI, Anthropic, Gemini, Mistral) with tool use for shell commands, file I/O, and background processes.

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
| `/help`              | Show available commands                       |
| `Ctrl-D`             | Quit                                          |

## Tools

The agent has only access to the following tools for now:

- **shell** — execute a command and return its output
- **spawn** — run a process in the background
- **file_read** — read file contents (supports partial reads)
- **file_write** — write to a file (supports partial writes)

## License

MIT. See [LICENSE](LICENSE).
