# bedrock-data-extractor

Tracks every Bedrock Dedicated Server (BDS) build minecraft.net offers, keeping each one's downloads here in
[versions/](versions), and extracts what each release says into [bedrock-data](https://github.com/mc-zuri/bedrock-data).
bedrock-data's README is the format of everything written there;
[bedrock-data-generator](https://github.com/mc-zuri/bedrock-data-generator) turns it into minecraft-data's bedrock files.

```
schedule ─> track ─> versions/ on main ─push─> extract ─> bedrock-data main ─> generator ─> minecraft-data fork
```

|                                      | from                                                                   | into                                             |
| ------------------------------------ | ---------------------------------------------------------------------- | ------------------------------------------------ |
| each build's zips (release, preview) | the download links API, and the zips themselves: sha256, size          | here: `versions/<channel>/<build>.json`          |
| a release's en_US strings            | `resource_packs/*/texts/en_US.lang` in the zip, read without unpacking | bedrock-data: `release/<build>/language.json.gz` |

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
pnpm extract                      # each tracked release bedrock-data has not got as versions/ has it now
pnpm extract 1.26.52.3 --force    # given releases, again
pnpm status                       # each tracked build, and whether bedrock-data has its data
```

- `track` asks the [download links API](https://net-secondary.web.minecraft-services.net/api/v1.0/download/links) for
  the latest zip of each channel and platform. A build new to `versions/` has each platform's zip hashed as it
  downloads (nothing is kept on disk; a platform minecraft.net has no zip of is left out), and
  `versions/<channel>/<build>.json` written. A build already tracked costs nothing, so a run that finds nothing new
  makes one request. The API only names the latest builds; an older one is found by its URL alone, so a build
  minecraft.net no longer lists can still be tracked by name.
- `extract` takes a release from `versions/`, downloads its linux zip (windows's where it has no linux one) into
  `CACHE_DIR`, refuses it unless it is the zip `versions/` hashed, reads its data out, and writes it with its version
  file (as `build.json`) into bedrock-data. A release bedrock-data has with the zips `versions/` has now is skipped. The
  zip is deleted once its release is done; one that failed keeps it for the next try. `JOBS` builds at a time.
- Each step rebuilds its index: `versions/versions.json` (track), bedrock-data's `versions.json` (extract).

Every file is checked against its schema before it is written (`schemas/` here for `versions/`, bedrock-data's own
`schemas/` there), and written only where what it says changed (a gzip, where what it decompresses to changed): a step
run again, on any machine, changes nothing. Nothing is committed: review `git status` here and in bedrock-data, and
commit.

## Workflows

- [track](.github/workflows/track.yml): every 6 hours (and by hand, with `builds` to track those), `pnpm track`,
  committed to this repo's main through its own deploy key, so that the push runs extract.
- [extract](.github/workflows/extract.yml): on a push to main that changes `versions/`, `src/` or the dependencies (and
  by hand, with `builds`/`force`). A push that changes the extraction extracts every release again; one that only
  tracks new builds extracts the releases bedrock-data lacks. The commit goes straight to bedrock-data's main. The
  generator picks it up on its schedule, half an hour after track's (or at once, through bedrock-data's
  notify-generator workflow, where that has a token).
- [CI](.github/workflows/ci.yml): `pnpm verify` and `pnpm test`, against bedrock-data's schemas.

They need:

|                                  |                                                                                                          |
| -------------------------------- | -------------------------------------------------------------------------------------------------------- |
| variable `BEDROCK_DATA_REPO`     | `mc-zuri/bedrock-data`                                                                                   |
| secret `BEDROCK_DATA_DEPLOY_KEY` | the private half of a write deploy key of bedrock-data: it can push there and nowhere else               |
| secret `EXTRACTOR_DEPLOY_KEY`    | the private half of a write deploy key of this repo: its pushes run workflows, a `GITHUB_TOKEN`'s do not |

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
| `src/lang.ts`         | a `.lang` file's keys and strings (bedrock-data README: language.json.gz) |
| `src/language.ts`     | each pack's `en_US.lang` out of a zip                                     |
| `src/dataRepo.ts`     | the bedrock-data checkout                                                 |
| `src/extract.ts`      | a tracked release into bedrock-data                                       |
| `src/files.ts`        | schema checks, and writes only where a file changed                       |
| `src/main.ts`         | the steps                                                                 |

A fix to what is extracted is made here; its push to main extracts every release again. bedrock-data and `versions/`
are never edited by hand. A change to a bedrock-data file's format changes bedrock-data's README and schemas in the
same change, and bumps its `formatVersion` where a reader must know.

## Next

The native agent of the monolith this was split from (`D:\projects\mc-zuri2\v2\bedrock-data-generator`: `native/`,
xmake + Zydis, injected on Windows, preloaded on Linux) moves here one kind of data at a time: each kind gets its
format in bedrock-data's README and schemas, its extraction here, and its generator there. The small kinds first
(biomes, effects, enchantments), then blocks and items. Its Linux build can run in the extract workflow.
