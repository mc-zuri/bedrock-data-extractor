# Development

`pnpm verify` (tsc, oxlint, prettier, clang-format) and `pnpm test` (node:test). Node 24 runs the `.ts` files as they
are (type stripping): there is no build step. The tests download nothing and start no server: a test that needs a zip
writes a small one (`test/helpers.ts`: `writeZip`, and `cachedRelease` for a tracked release whose zips are already in
the cache), and one that needs a release's blocks hands extract a stand-in for the agent (`fakeBlocks`). The
bedrock-data tests use the schemas of `BEDROCK_DATA_DIR`.

The agent (`native/`) builds with [xmake](https://xmake.io): `pnpm native`. Windows needs MSVC (Visual Studio Build
Tools, "Desktop development with C++"); Linux clang (C++20). A Linux server runs on Linux only: on Windows, run the
Linux steps in WSL. The Linux agent links its C++ runtime in and asks glibc for nothing newer than 2.36, so one built
on a newer distribution runs on an older host (Debian 12) as it is: `libbdg_agent.so` and the extractor are all such
a host needs, with Node and OpenSSL 1.1.

## Code

|                       |                                                                               |
| --------------------- | ----------------------------------------------------------------------------- |
| `src/main.ts`         | the steps                                                                     |
| `src/mojang.ts`       | the download links API: each channel's and platform's latest build            |
| `src/versions.ts`     | builds, channels, platforms; where each zip is                                |
| `src/download.ts`     | a zip, exactly at its URL, hashed while it downloads                          |
| `src/zip.ts`          | a zip's entries read, or unpacked                                             |
| `src/versionStore.ts` | `versions/`: each tracked build's zips                                        |
| `src/track.ts`        | a build into `versions/`                                                      |
| `src/support.ts`      | a build marked supported or not, platform by platform                         |
| `src/language.ts`     | each pack's `en_US.lang` out of a zip                                         |
| `src/lang.ts`         | a `.lang` file's keys and strings (bedrock-data README: language.json.gz)     |
| `src/platform.ts`     | what differs between the OSes a server runs on, and the agent's build         |
| `src/server.ts`       | a server unpacked and started on a fresh world                                |
| `src/agent.ts`        | the agent run in a server, its result waited for                              |
| `src/client.ts`       | a client joined to a server: what it is sent of the items                     |
| `src/protocol.ts`     | what each preview's protocol changed since the version bedrock-protocol knows |
| `src/blocks.ts`       | what the agent wrote, read, with what the client was sent                     |
| `src/loot.ts`         | the loot tables the vanilla behavior packs' blocks name                       |
| `src/checks.ts`       | what exported data must hold before it is written                             |
| `src/dataRepo.ts`     | the bedrock-data checkout                                                     |
| `src/extract.ts`      | a supported build into bedrock-data                                           |
| `src/files.ts`        | schema checks, and writes only where a file changed                           |

|                    |                                                                                          |
| ------------------ | ---------------------------------------------------------------------------------------- |
| `native/agent/`    | what the agent does in a server; how it gets in on each OS                               |
| `native/bindings/` | each build's bindings: `Steps.cpp` (Windows), `StepsLinux.cpp`, names in `Names.h`       |
| `native/game/`     | the game's objects: the level, the registries, states, facts, dig, drops, map colours    |
| `native/export/`   | the files the agent writes                                                               |
| `native/memory/`   | fault-safe reads, the pattern scanner, the hook, ELF files, RTTI, a function's data flow |
| `native/inject/`   | `bdg_inject.exe`, which loads the agent into a Windows server                            |
| `native/check/`    | `bdg_check`, which resolves the bindings against a server on disk                        |

## A new build

Each step in `native/bindings/` states only what changed since the one before; a build runs every step up to it, in
Bedrock's order (`native/core/Version.h`, `compareVersions`): by the first three numbers, then that version's previews
(the fourth number from 20), then its releases (below 20), which come out after them, so 1.26.60.5 runs 1.26.60.28's
steps. A new build the agent cannot bind yet fails support (the workflow opens an issue):

1. `pnpm check <build>` (on its platform) shows what does not resolve. `bdg_check <exe> --find "<pattern>"` (Windows)
   tries a pattern without rebuilding.
2. Add a step for what moved: a pattern, a slot, an offset. Slots and offsets are not checked by `check`: a wrong one
   shows in the run (`cache/<channel>/<build>/<platform>/out/agent.log`) or in the checks.
3. `pnpm support <build>`, and compare what it exports with the build before.

What the agent finds itself (the members `game/` finds by content, the virtuals it finds by their code) needs no step;
its log says where each was found, which a step can pin where a build has no one place that fits.

## Fixing an export

A wrong value is fixed here, never by editing bedrock-data or `versions/` by hand, and never by a layer of hardcoded
corrections. Merge the fix, then export the releases it changes again: the extract workflow by hand with `builds` and
`force` (locally, `pnpm extract <builds> --force`).

A change to a bedrock-data file's format changes bedrock-data's README and schemas in the same change, and bumps its
`formatVersion` where a reader must know.
