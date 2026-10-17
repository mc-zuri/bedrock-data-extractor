# Development

`pnpm verify` (tsc, oxlint, prettier) and `pnpm test` (node:test). Node 24 runs the `.ts` files as they are (type
stripping): there is no build step. The tests download nothing: a test that needs a zip writes a small one
(`test/helpers.ts`: `writeZip`, and `cachedRelease` for a tracked release whose zips are already in the cache), and
the bedrock-data tests use the schemas of `BEDROCK_DATA_DIR`.

## Code

|                       |                                                                           |
| --------------------- | ------------------------------------------------------------------------- |
| `src/main.ts`         | the steps                                                                 |
| `src/mojang.ts`       | the download links API: each channel's and platform's latest build        |
| `src/versions.ts`     | builds, channels, platforms; where each zip is                            |
| `src/download.ts`     | a zip, exactly at its URL, hashed while it downloads                      |
| `src/versionStore.ts` | `versions/`: each tracked build's zips                                    |
| `src/track.ts`        | a build into `versions/`                                                  |
| `src/support.ts`      | a release made supported, platform by platform                            |
| `src/language.ts`     | each pack's `en_US.lang` out of a zip                                     |
| `src/lang.ts`         | a `.lang` file's keys and strings (bedrock-data README: language.json.gz) |
| `src/checks.ts`       | what exported data must hold before it is written                         |
| `src/dataRepo.ts`     | the bedrock-data checkout                                                 |
| `src/extract.ts`      | a supported release into bedrock-data                                     |
| `src/files.ts`        | schema checks, and writes only where a file changed                       |

## Fixing an export

A wrong value is fixed here, never by editing bedrock-data or `versions/` by hand, and never by a layer of hardcoded
corrections. Merge the fix, then export the releases it changes again: the extract workflow by hand with `builds` and
`force` (locally, `pnpm extract <builds> --force`).

A change to a bedrock-data file's format changes bedrock-data's README and schemas in the same change, and bumps its
`formatVersion` where a reader must know.

## Next

The native agent of the monolith this was split from (`D:\projects\mc-zuri2\v2\bedrock-data-generator`: `native/`,
xmake + Zydis, injected on Windows, preloaded on Linux) moves here one kind of data at a time. Each kind gets:

- its format, in bedrock-data's README and schemas;
- its export and checks, here;
- its generator, in bedrock-data-generator.

The small kinds come first (biomes, effects, enchantments), then blocks and items. Its bindings differ by platform:
a new build the agent cannot bind yet fails support on that platform, and its issue is where its bindings are added.
