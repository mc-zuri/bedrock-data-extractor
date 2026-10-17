# bedrock-data-extractor

Tracks every Bedrock Dedicated Server (BDS) build minecraft.net offers, and extracts what each says into
[bedrock-data](https://github.com/mc-zuri/bedrock-data). bedrock-data's README is the format of everything written
there; [bedrock-data-generator](../bedrock-data-generator) turns it into minecraft-data's bedrock files.

What it extracts so far:

|                                        | from                                                                   | into                               |
| -------------------------------------- | ---------------------------------------------------------------------- | ---------------------------------- |
| each build's downloads (both channels) | the zips themselves: sha256, size                                      | `<channel>/<build>/build.json`     |
| a release's en_US strings              | `resource_packs/*/texts/en_US.lang` in the zip, read without unpacking | `release/<build>/language.json.gz` |

The native agent (C++, run inside the server) comes next, one kind of data at a time; see [Next](#next).

## Setup

Node 24 and pnpm. A bedrock-data checkout next to this one (`BEDROCK_DATA_DIR`, default `../bedrock-data`):

```sh
git clone https://github.com/mc-zuri/bedrock-data ../bedrock-data
pnpm install
cp .env.example .env   # optional: BEDROCK_DATA_DIR, CACHE_DIR, JOBS
```

## Steps

```sh
pnpm track                        # what minecraft.net offers now, both channels
pnpm extract 1.21.42.01 1.26.52.3 # given builds (as their zips name them); --channel=preview; --force: again
pnpm status                       # each build kept, its downloads and data
```

- `track` asks the [download links API](https://net-secondary.web.minecraft-services.net/api/v1.0/download/links)
  for the latest zip of each channel and platform. A build already kept with all it should have is skipped without a
  download, so a run that finds nothing new makes one request.
- `extract` downloads each platform's zip of a build (a platform minecraft.net has no zip of is left out), hashes it,
  reads a release's data out of linux's zip (windows's where there is no linux one), writes it, and deletes the zips.
  A build that fails keeps what it downloaded in `CACHE_DIR` for the next try. `JOBS` builds at a time.
- Each step rebuilds `versions.json` from the `build.json` files there are.

Every file is checked against bedrock-data's own `schemas/` before it is written, and written only where what it says
changed (a gzip, where what it decompresses to changed): a step run again, on any machine, leaves bedrock-data as it
is. Nothing is committed: review `git -C ../bedrock-data status` and commit there.

The API only names the latest builds. An older one is found by its URL alone, so a build minecraft.net no longer lists
can still be extracted by name; the release builds minecraft-data has were backfilled so:

```sh
pnpm extract 1.16.201.02 1.16.210.05 1.16.220.02 1.17.0.03 1.17.10.04 1.17.30.04 1.17.40.06 1.18.0.02 1.18.11.01 \
  1.18.30.04 1.19.1.01 1.19.10.03 1.19.20.02 1.19.21.01 1.19.30.04 1.19.40.02 1.19.50.02 1.19.60.04 1.19.62.01 \
  1.19.63.01 1.19.70.02 1.19.80.02 1.20.0.01 1.20.11.01 1.20.15.01 1.20.30.02 1.20.40.01 1.20.50.03 1.20.61.01 \
  1.20.71.01 1.20.80.05 1.21.0.03 1.21.2.02 1.21.20.03 1.21.30.03 1.21.42.01 1.21.50.07 1.21.50.10 1.21.60.10 \
  1.21.70.04 1.21.80.3 1.21.90.3 1.21.93.1 1.21.100.6 1.21.111.1 1.21.120.4 1.21.124.2 1.21.130.3 1.26.0.2 \
  1.26.10.4 1.26.20.5 1.26.30.5 1.26.40.8 1.26.45.1 1.26.51.1
```

## Workflows

- [update](.github/workflows/update.yml): every 6 hours, `pnpm track` into bedrock-data, committed and pushed straight
  to its main. Run by hand with `builds` to extract those instead (with `force`: again). The generator picks a new
  commit up on its schedule, half an hour later (or at once, through bedrock-data's notify-generator workflow, where
  that has a token).
- [CI](.github/workflows/ci.yml): `pnpm verify` and `pnpm test`, against bedrock-data's schemas.

They need:

|                                  |                                                                                         |
| -------------------------------- | --------------------------------------------------------------------------------------- |
| variable `BEDROCK_DATA_REPO`     | `mc-zuri/bedrock-data`                                                                  |
| secret `BEDROCK_DATA_DEPLOY_KEY` | the private half of a write deploy key of that repo: it can push there and nowhere else |

## Development

`pnpm verify` (tsc, oxlint, prettier) and `pnpm test` (node:test). Node runs the `.ts` files as they are (type
stripping): no build step.

|                   |                                                                                |
| ----------------- | ------------------------------------------------------------------------------ |
| `src/mojang.ts`   | the download links API: each channel's and platform's latest build             |
| `src/versions.ts` | builds, channels, platforms; where each zip is                                 |
| `src/download.ts` | a zip, exactly at its URL, hashed while it downloads                           |
| `src/lang.ts`     | a `.lang` file's keys and strings (bedrock-data README: language.json.gz)      |
| `src/language.ts` | each pack's `en_US.lang` out of a zip                                          |
| `src/dataRepo.ts` | the bedrock-data checkout: schema checks, writes only on change, versions.json |
| `src/extract.ts`  | a build into bedrock-data                                                      |
| `src/main.ts`     | the steps                                                                      |

A fix to what is extracted is made here and the builds extracted again (`pnpm extract <builds> --force`), never by
editing bedrock-data. A change to a file's format changes bedrock-data's README and schemas in the same change, and
bumps its `formatVersion` where a reader must know.

## Next

The native agent of the monolith this was split from (`D:\projects\mc-zuri2\v2\bedrock-data-generator`: `native/`,
xmake + Zydis, injected on Windows, preloaded on Linux) moves here one kind of data at a time: each kind gets its
format in bedrock-data's README and schemas, its extraction here, and its generator there. The small kinds first
(biomes, effects, enchantments), then blocks and items. Its Linux build can run in the update workflow.
