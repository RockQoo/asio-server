# Repository Guidelines

## Project Structure & Module Organization

This is a C++20, x64-only Visual Studio solution. `Shared/Core/Src/` contains reusable networking, packet, threading, timer, logging, and task infrastructure and builds as a static library. `GatewayServer/`, `WorldServer/`, and `ZoneServer/` contain the distributed server processes; keep game-state logic in the owning server rather than `Core`. `TestClient/` is the interactive protocol client, while `LoadTestClient/` drives concurrent-session tests. Documentation lives in `docs/`, launch scripts in `bat/`, and vendored standalone Asio in `3rd/asio/`. Build outputs under `bin/`, `obj/`, and logs are generated and must not be committed.

## Build, Test, and Development Commands

Run commands from a Visual Studio 2022 Developer PowerShell (MSVC v143):

```powershell
MSBuild.exe asio-server.slnx -p:Configuration=Debug -p:Platform=x64 -m
bat\server.bat
bat\client.bat
bin\x64\Debug\LoadTestClient.exe 127.0.0.1 9000 1000 200
```

The first command builds all six projects; use `Release` for optimized binaries. `server.bat` starts World, Zone, then Gateway, and `client.bat` opens the REPL. The load-client example starts 1,000 sessions in batches of 200.

## Coding Style & Naming Conventions

Use four-space indentation, Allman braces, and UTF-8 without BOM. Types, functions, and namespaces use PascalCase; private members use camelCase with a trailing underscore (`sendQueue_`). Use fixed-width integers, `enum class` with an explicit underlying type, C++ casts, and `LOG` instead of console output outside client UI. A `.cpp` starts with its project `pch.h`, then its matching header, other solution headers, third-party headers, and standard headers in separated groups. Preserve `/utf-8` in project settings. Never edit `3rd/asio`; consult `.claude/rules/cpp-patterns.md` for detailed ownership, getter, and exception-safety rules.

## Testing Guidelines

There is no automated unit-test framework or coverage threshold. Build the full solution, launch the server stack, and exercise `echo`, `move`, `chat`, and `mail` commands through `TestClient`. Use `LoadTestClient` for concurrency-sensitive changes and inspect `logs/` for errors. Document the exact commands and scenarios tested in the pull request.

## Commit & Pull Request Guidelines

Recent commits use concise, feature-scoped summaries without conventional prefixes, for example `ZoneServer 초기 구현...`. Keep each commit focused and describe the subsystem plus outcome. Pull requests should summarize architectural impact, link relevant issues, list validation performed, and call out protocol or thread-ownership changes. Include screenshots when changing `docs/flowcharts/` or other rendered documentation.
