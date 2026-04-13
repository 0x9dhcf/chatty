# Changelog

All notable changes to this project are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `web_search` tool: Tavily-backed web search (synthesized answer + ranked results). Registered only when `TAVILY_API_KEY` is set.
- `web_extract` tool: Tavily-backed page extraction returning cleaned markdown for one or more URLs. Registered only when `TAVILY_API_KEY` is set.
- System prompt gains a parsimony rule for the web tools when present: prefer search, only extract on a specific URL or when snippets were insufficient, default to `extract_depth=basic`.
- Debug builds now log each LLM round-trip and tool call (with elapsed time) to stderr.

### Changed
- Build now requires `libcurl` (used by the web tools).

[Unreleased]: https://github.com/0x9dhcf/chatty/compare/HEAD...HEAD
