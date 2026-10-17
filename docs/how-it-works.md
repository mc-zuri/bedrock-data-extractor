# How it works

| file                              | holds                                                                    | written by                                               |
| --------------------------------- | ------------------------------------------------------------------------ | -------------------------------------------------------- |
| `versions/<channel>/<build>.json` | a build (release or preview) and each platform's zip: URL, sha256, size  | `pnpm track`                                             |
|                                   | each zip's `supported`: whether the release is exported from it          | `pnpm track` (`false`), `pnpm support` (`true`, `false`) |
| `versions/versions.json`          | each channel's builds, newest first                                      | `pnpm track`                                             |
| bedrock-data `release/<build>/…`  | what was exported from a release (bedrock-data's README says the format) | `pnpm extract`                                           |
| bedrock-data `versions.json`      | the releases bedrock-data has                                            | `pnpm extract`                                           |

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
pnpm native
pnpm check <builds...> [--platform=linux|windows]
pnpm status
```

- **track** asks the [download links API](https://net-secondary.web.minecraft-services.net/api/v1.0/download/links)
  for the latest zip of each channel and platform (or takes the builds named). A build new to `versions/` has each
  platform's zip hashed as it downloads (nothing is kept on disk; a platform minecraft.net has no zip of is left out)
  and starts unsupported. A build already tracked costs nothing, so a run that finds nothing new makes one request.
  Tracking a build again (`--force`) keeps a platform supported only where its zip is the same. The API only names the
  latest builds; an older one is found by its URL alone.
- **support** reads and checks a tracked release's zip as extract would, without writing to bedrock-data, and marks
  the platform `"supported": true` where it passes, `false` where it fails. It runs the platform of this machine (its
  OS: a server runs only there), or the one `--platform` names, which must be it. Previews are never supported.
- **extract** takes each release supported on some platform that bedrock-data has not got every kind of (or the
  builds named), and:
  1. downloads the zip of the platform it is supported on (linux's where both are) into `CACHE_DIR`;
  2. refuses it unless it is the zip `versions/` hashed;
  3. reads its language out of the zip, without unpacking it: each resource pack's `texts/en_US.lang`;
  4. unpacks its server, starts it on a new world, and has the agent write its blocks: the palette and each state's
     shapes ([Agent](#agent));
  5. checks the data (`src/checks.ts`);
  6. writes it into bedrock-data, with `build.json`: the release, each zip's URL, sha256 and size, and which platform
     each kind was read from.

  A release bedrock-data has every kind of is skipped unless `--force`d. A release exported from another platform than
  this machine's is left for one that runs it (named, it fails). A zip and its server are deleted once the release is
  done; one that failed keeps them, and the agent's log. `JOBS` builds at a time, each server on ports of its own.

- **native** builds the agent for this machine's OS; **check** resolves its bindings against each named release's
  server without starting it, and prints every binding where one is named.
- **status** prints each tracked build, each platform as `supported`, `zip` (not supported) or `-` (no zip), and
  whether bedrock-data has its data.

## Agent

The blocks are what the running server says, so they are read inside it: `native/` is an agent the server loads.

- **Windows**: `bdg_inject.exe` loads `bdg_agent.dll` into `bedrock_server.exe` once it prints "Server started".
- **Linux**: `libbdg_agent.so` is preloaded (`LD_PRELOAD`), and starts once node writes `<out>/go`.

It binds what it calls for the running build (`native/bindings/`: Windows by byte patterns, slots and offsets; Linux
by the server's own symbols where it ships them, 1.16.201 to 1.21.2 but 1.17.0, else as Windows), hooks `Level::tick`,
and on the tick finds the overworld (creating it where it does not exist yet) and walks the block type registry: every
block type and its states in runtime id order. For each state it writes the game's own CompoundTag
(`block_palette.nbt`) and asks the game for its shapes (`block_shapes.nbt`): the collision boxes (at the origin and 16
blocks away), the visual and UI boxes, the outline, and from 1.20.30 the camera box. Each call runs on a worker thread
with a deadline, and a fault in the game is caught; either fails the export, never the server's tick. Then it writes
`result.txt`, and node kills the server.

A platform's server answers as its build does, so the platforms can disagree; bedrock-data's README lists where, and
`build.json` says which platform each file came from.

## What every write keeps to

- Each file is checked against its schema first: `schemas/` here, bedrock-data's own `schemas/` there.
- A file is written only where what it says changed (a gzip, where what it decompresses to changed), and holds no
  time or extractor version: a step run again, on any machine of the same platform, changes nothing.
- Nothing is committed: review `git status` here and in bedrock-data, and commit.

## Settings

`.env` (see `.env.example`): `BEDROCK_DATA_DIR` (default `../bedrock-data`), `CACHE_DIR` (default `cache`), `JOBS`
(default 3), `SERVER_PORT_BASE` (default 41032). On Linux, the servers before 1.19.40 need OpenSSL 1.1 on
`LD_LIBRARY_PATH`.
