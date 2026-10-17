# How it works

| file                              | holds                                                                    | written by                                      |
| --------------------------------- | ------------------------------------------------------------------------ | ----------------------------------------------- |
| `versions/<channel>/<build>.json` | a build (release or preview) and each platform's zip: URL, sha256, size  | `pnpm track`                                    |
|                                   | each zip's `supported`: whether the release is exported from it          | `pnpm track` (`false`), `pnpm support` (`true`) |
| `versions/versions.json`          | each channel's builds, newest first                                      | `pnpm track`                                    |
| bedrock-data `release/<build>/…`  | what was exported from a release (bedrock-data's README says the format) | `pnpm extract`                                  |
| bedrock-data `versions.json`      | the releases bedrock-data has                                            | `pnpm extract`                                  |

A build is named as its zip names it: `1.21.42.01` (`bedrock-server-1.21.42.01.zip`), never `1.21.42.1`.

```json
{
  "build": "1.26.52.3",
  "version": "1.26.52",
  "channel": "release",
  "binary": {
    "linux": { "url": "https://www.minecraft.net/bedrockdedicatedserver/bin-linux/bedrock-server-1.26.52.3.zip", "sha256": "…", "size": 94927522, "supported": true },
    "windows": { "url": "https://www.minecraft.net/bedrockdedicatedserver/bin-win/bedrock-server-1.26.52.3.zip", "sha256": "…", "size": 85766941, "supported": true }
  }
}
```

## Steps

```sh
pnpm track [builds...] [--channel=preview] [--force]
pnpm support <builds...> [--platform=linux|windows]
pnpm extract [builds...] [--force]
pnpm status
```

- **track** asks the [download links API](https://net-secondary.web.minecraft-services.net/api/v1.0/download/links)
  for the latest zip of each channel and platform (or takes the builds named). A build new to `versions/` has each
  platform's zip hashed as it downloads (nothing is kept on disk; a platform minecraft.net has no zip of is left out)
  and starts unsupported. A build already tracked costs nothing, so a run that finds nothing new makes one request.
  Tracking a build again (`--force`) keeps a platform supported only where its zip is the same. The API only names the
  latest builds; an older one is found by its URL alone.
- **support** reads and checks each platform's zip of tracked releases as extract would, without writing to
  bedrock-data, and marks the platforms that pass `"supported": true`. A platform that fails is told, and stays as it
  was. Previews are never supported.
- **extract** takes each release supported on some platform that bedrock-data has not got the data of (or the builds
  named), and:
  1. downloads the zip of a platform it is supported on (linux's where both are) into `CACHE_DIR`;
  2. refuses it unless it is the zip `versions/` hashed;
  3. reads its data out without unpacking it: each resource pack's `texts/en_US.lang`;
  4. checks the data (`src/checks.ts`: at least 5000 vanilla strings, keys minecraft-data's language schema accepts);
  5. writes it into bedrock-data, with `build.json`: the release, each zip's URL, sha256 and size, and what was read
     from which.

  A release bedrock-data has the data of is skipped unless `--force`d. A zip is deleted once its release is done; one
  that failed keeps it for the next try. `JOBS` builds at a time.

- **status** prints each tracked build, each platform as `supported`, `zip` (not supported) or `-` (no zip), and
  whether bedrock-data has its data.

## What every write keeps to

- Each file is checked against its schema first: `schemas/` here, bedrock-data's own `schemas/` there.
- A file is written only where what it says changed (a gzip, where what it decompresses to changed), and holds no
  time or extractor version: a step run again, on any machine, changes nothing.
- Nothing is committed: review `git status` here and in bedrock-data, and commit.

## Settings

`.env` (see `.env.example`): `BEDROCK_DATA_DIR` (default `../bedrock-data`), `CACHE_DIR` (default `cache`), `JOBS`
(default 3).
