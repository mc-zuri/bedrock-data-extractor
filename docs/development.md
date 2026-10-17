# Development

`pnpm verify` (tsc, oxlint, prettier, clang-format) and `pnpm test` (node:test). Node 24 runs the `.ts` files as they
are (type stripping): there is no build step. The tests download nothing and start no server: a test that needs a zip
writes a small one (`test/helpers.ts`: `writeZip`, and `cachedRelease` for a tracked release whose zips are already in
the cache), and one that needs a release's blocks hands extract a stand-in for the agent (`fakeBlocks`). The
bedrock-data tests use the schemas of `BEDROCK_DATA_DIR`.

The agent (`native/`) builds with [xmake](https://xmake.io): `pnpm native`. Windows needs MSVC (Visual Studio Build
Tools, "Desktop development with C++"); Linux clang (C++20). A Linux server runs on Linux only: on Windows, run the
Linux steps in WSL.

## Code

|                       |                                                                           |
| --------------------- | ------------------------------------------------------------------------- |
| `src/main.ts`         | the steps                                                                 |
| `src/mojang.ts`       | the download links API: each channel's and platform's latest build        |
| `src/versions.ts`     | builds, channels, platforms; where each zip is                            |
| `src/download.ts`     | a zip, exactly at its URL, hashed while it downloads                      |
| `src/zip.ts`          | a zip's entries read, or unpacked                                         |
| `src/versionStore.ts` | `versions/`: each tracked build's zips                                    |
| `src/track.ts`        | a build into `versions/`                                                  |
| `src/support.ts`      | a release marked supported or not, platform by platform                   |
| `src/language.ts`     | each pack's `en_US.lang` out of a zip                                     |
| `src/lang.ts`         | a `.lang` file's keys and strings (bedrock-data README: language.json.gz) |
| `src/platform.ts`     | what differs between the OSes a server runs on, and the agent's build     |
| `src/server.ts`       | a server unpacked and started on a fresh world                            |
| `src/agent.ts`        | the agent run in a server, its result waited for                          |
| `src/blocks.ts`       | the palette and shapes the agent wrote, read                              |
| `src/checks.ts`       | what exported data must hold before it is written                         |
| `src/dataRepo.ts`     | the bedrock-data checkout                                                 |
| `src/extract.ts`      | a supported release into bedrock-data                                     |
| `src/files.ts`        | schema checks, and writes only where a file changed                       |

|                    |                                                                                        |
| ------------------ | -------------------------------------------------------------------------------------- |
| `native/agent/`    | what the agent does in a server; how it gets in on each OS                             |
| `native/bindings/` | each build's bindings: `Steps.cpp` (Windows), `StepsLinux.cpp`, names in `Names.h`     |
| `native/game/`     | the game's objects: the level, the block type registry, states, shapes                 |
| `native/export/`   | the files the agent writes                                                             |
| `native/memory/`   | fault-safe reads, the pattern scanner, the hook, the Linux server's ELF files and RTTI |
| `native/inject/`   | `bdg_inject.exe`, which loads the agent into a Windows server                          |
| `native/check/`    | `bdg_check`, which resolves the bindings against a server on disk                      |

## A new build

Each step in `native/bindings/` states only what changed since the one before; a build runs every step up to it. A new
build the agent cannot bind yet fails support (the workflow opens an issue):

1. `pnpm check <build>` (on its platform) shows what does not resolve. `bdg_check <exe> --find "<pattern>"` (Windows)
   tries a pattern without rebuilding.
2. Add a step for what moved: a pattern, a slot, an offset. Slots and offsets are not checked by `check`: a wrong one
   shows in the run (`cache/release/<build>/<platform>/out/agent.log`) or in the checks.
3. `pnpm support <build>`, and compare its shapes with the build before.

## Fixing an export

A wrong value is fixed here, never by editing bedrock-data or `versions/` by hand, and never by a layer of hardcoded
corrections. Merge the fix, then export the releases it changes again: the extract workflow by hand with `builds` and
`force` (locally, `pnpm extract <builds> --force`).

A change to a bedrock-data file's format changes bedrock-data's README and schemas in the same change, and bumps its
`formatVersion` where a reader must know.
