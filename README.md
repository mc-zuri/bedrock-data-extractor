# bedrock-data-extractor

Tracks every Bedrock Dedicated Server (BDS) build minecraft.net offers, keeping each one's downloads here in
[versions/](versions), and exports what each supported release says into
[bedrock-data](https://github.com/mc-zuri/bedrock-data). bedrock-data's README is the format of everything written
there; [bedrock-data-generator](https://github.com/mc-zuri/bedrock-data-generator) turns it into minecraft-data's
bedrock files.

```
track (schedule)    what minecraft.net offers ─> versions/ ─> commit to main
  └─ each new release ─> support
support (by hand, or from track)
    add to supported.json, export, test
      ok   ─> pull request: "Support BDS <build>"
      fail ─> issue: "Support BDS <build>", with the log
extract (push to main: a merged support PR, or a change to the export)
    each supported release bedrock-data lacks ─> commit to bedrock-data main ─> generator ─> minecraft-data fork
```

|                                      | from                                                                   | into                                             |
| ------------------------------------ | ---------------------------------------------------------------------- | ------------------------------------------------ |
| each build's zips (release, preview) | the download links API, and the zips themselves: sha256, size          | here: `versions/<channel>/<build>.json`          |
| the releases exported                | merged support pull requests                                           | here: `supported.json`                           |
| a supported release's en_US strings  | `resource_packs/*/texts/en_US.lang` in the zip, read without unpacking | bedrock-data: `release/<build>/language.json.gz` |

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
pnpm track                        # what minecraft.net offers now, both channels, into versions/
pnpm track 1.21.42.01 1.26.52.3   # given builds (as their zips name them); --channel=preview; --force: again
pnpm support 1.26.52.3            # add tracked releases to supported.json
pnpm extract                      # each supported release bedrock-data has not got the data of
pnpm extract 1.26.52.3 --force    # given releases, again
pnpm status                       # each tracked build, whether it is supported, whether bedrock-data has its data
```

- `track` asks the [download links API](https://net-secondary.web.minecraft-services.net/api/v1.0/download/links) for
  the latest zip of each channel and platform. A build new to `versions/` has each platform's zip hashed as it
  downloads (nothing is kept on disk; a platform minecraft.net has no zip of is left out), and
  `versions/<channel>/<build>.json` written. A build already tracked costs nothing, so a run that finds nothing new
  makes one request. The API only names the latest builds; an older one is found by its URL alone, so a build
  minecraft.net no longer lists can still be tracked by name.
- `support` adds tracked releases to `supported.json`. Previews are never supported.
- `extract` takes a release (each supported one, or those named), downloads its linux zip (windows's where it has no
  linux one) into `CACHE_DIR`, refuses it unless it is the zip `versions/` hashed, reads its data out, checks it
  (`src/checks.ts`: what the schemas cannot say), and writes it with its version file (as `build.json`) into
  bedrock-data. A release bedrock-data has the data of is skipped unless forced. The zip is deleted once its release is
  done; one that failed keeps it for the next try. `JOBS` builds at a time.
- `track` rebuilds `versions/versions.json`, `extract` bedrock-data's `versions.json`.

Every file is checked against its schema before it is written (`schemas/` here, bedrock-data's own `schemas/` there),
and written only where what it says changed (a gzip, where what it decompresses to changed): a step run again, on any
machine, changes nothing. Nothing is committed: review `git status` here and in bedrock-data, and commit.

## Workflows

- [track](.github/workflows/track.yml): every 6 hours (and by hand, with `builds` to track those), `pnpm track`,
  committed to main; then the support workflow is run for each release it added.
- [support](.github/workflows/support.yml): by hand with a `build`, or from track. `pnpm support`, `pnpm extract
--force` (into a copy of bedrock-data, kept only as the run's artifact) and `pnpm test`. Where all pass, it opens (or
  refreshes) the pull request `support/<build>` that adds the build to `supported.json`; where one fails, it opens an
  issue "Support BDS <build>" with the log (or comments on the open one). A build the extraction cannot handle yet is
  supported by fixing the extraction and adding it to `supported.json` in the same pull request.
- [extract](.github/workflows/extract.yml): on a push to main that changes `supported.json`, `src/` or the
  dependencies (and by hand, with `builds`/`force`): `pnpm extract`, committed straight to bedrock-data's main. A
  release bedrock-data has the data of is left as it is: to export every release again after a fix, run it by hand
  with `force`. The generator picks the commit up on its schedule (or at once, through bedrock-data's
  notify-generator workflow, where that has a token).
- [CI](.github/workflows/ci.yml): `pnpm verify` and `pnpm test`, against bedrock-data's schemas.

They need:

|                                  |                                                                                            |
| -------------------------------- | ------------------------------------------------------------------------------------------ |
| variable `BEDROCK_DATA_REPO`     | `mc-zuri/bedrock-data`                                                                     |
| secret `BEDROCK_DATA_DEPLOY_KEY` | the private half of a write deploy key of bedrock-data: it can push there and nowhere else |
| Actions setting                  | "Allow GitHub Actions to create and approve pull requests", for support's pull requests    |

A pull request the support workflow opens runs no CI of its own (GitHub runs none for a `GITHUB_TOKEN`'s): the run
that opened it exported and tested it.

## Development

`pnpm verify` (tsc, oxlint, prettier) and `pnpm test` (node:test; no test downloads). Node runs the `.ts` files as
they are (type stripping): no build step.

|                       |                                                                           |
| --------------------- | ------------------------------------------------------------------------- |
| `src/mojang.ts`       | the download links API: each channel's and platform's latest build        |
| `src/versions.ts`     | builds, channels, platforms; where each zip is                            |
| `src/download.ts`     | a zip, exactly at its URL, hashed while it downloads                      |
| `src/versionStore.ts` | `versions/`: each tracked build's zips                                    |
| `src/track.ts`        | a build into `versions/`                                                  |
| `src/supported.ts`    | `supported.json`: the releases exported                                   |
| `src/lang.ts`         | a `.lang` file's keys and strings (bedrock-data README: language.json.gz) |
| `src/language.ts`     | each pack's `en_US.lang` out of a zip                                     |
| `src/checks.ts`       | what extracted data must hold before it is written                        |
| `src/dataRepo.ts`     | the bedrock-data checkout                                                 |
| `src/extract.ts`      | a tracked release into bedrock-data                                       |
| `src/files.ts`        | schema checks, and writes only where a file changed                       |
| `src/main.ts`         | the steps                                                                 |

A fix to what is extracted is made here, then exported again (`pnpm extract <builds> --force`, or the extract workflow
by hand with `force`). bedrock-data, `versions/` and `supported.json` are never edited by hand (`supported.json` only
through `pnpm support`). A change to a bedrock-data file's format changes bedrock-data's README and schemas in the same
change, and bumps its `formatVersion` where a reader must know.

## Next

The native agent of the monolith this was split from (`D:\projects\mc-zuri2\v2\bedrock-data-generator`: `native/`,
xmake + Zydis, injected on Windows, preloaded on Linux) moves here one kind of data at a time: each kind gets its
format in bedrock-data's README and schemas, its extraction here, and its generator there. The small kinds first
(biomes, effects, enchantments), then blocks and items. A new build the agent cannot bind yet fails support, and its
issue is where its bindings are added.
