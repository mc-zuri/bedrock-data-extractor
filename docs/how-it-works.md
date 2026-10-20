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
pnpm steve <builds...> [--port=19150] [--force] | pnpm steve --import=<dir> [builds...]
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
  3. reads its language out of the zip, without unpacking it: each resource pack's `texts/en_US.lang`. One that the
     pack's `contents.json` lists and the zip lacks is read from the release's other zip, refused where that has none
     (1.16.220.02's Linux zip has no `texts` in its chemistry pack; its Windows zip has);
  4. unpacks its server, starts it on a new flat world, joins it with a client (what the server sends a client: the
     item registry, the creative list and item_component; the biome definitions, the entity identifiers, the crafting
     recipes, the commands, and the player's attributes), has its console answer `help <command>` for each command sent
     (with cheats allowed: `changesetting allow-cheats true`), has the agent write what the game says of its blocks,
     items, enchantments, biomes, mob effects, enums and tints ([Agent](#agent)), and reads the vanilla behavior packs'
     definitions of blocks and entities and the loot tables they name;
  5. checks the data (`src/checks.ts`); before 1.26.20, what a Linux server gives otherwise than the Windows server
     only because their compilers round a float otherwise is made the Windows server's (`asOnWindows`: the chests' top
     `0.95`, not `0.95000005`; from 1.21.42, 21 blocks' explosion resistance, `4.2000003`, not `4.2`), and before
     1.26.40 the ids of the items a `vanilla_<version>` pack defines (the spears, bundles, trial keys): each pack's ids
     go to its items in the order NTFS lists their files (`src/packItems.ts`);
  6. writes it into bedrock-data, with `build.json`: the release, each zip's URL, sha256 and size, and which platform
     each kind was read from.

  A release bedrock-data has every kind of is skipped unless `--force`d. A release exported from another platform than
  this machine's is left for one that runs it (named, it fails). A zip and its server are deleted once the release is
  done; one that failed keeps them, and the agent's log. `JOBS` builds at a time, each server on ports of its own; the
  first failure stops the run (the servers running are ended, none is started after it).

- **steve** (by hand) keeps each named release's default skin (Steve), which no server holds: it listens on `--port`
  (19150) as a server of the release's version (bedrock-protocol's, no BDS: the skin is in the first packet a client
  sends) until a person joins with a real client of that version and the default skin, and writes the skin, every
  persona id in it made a fixed one (the account's is not kept), into `release/<build>/steve.json.gz` and its entry in
  `build.json`, one release at a time. `--import=<dir>` takes earlier captures instead, `<dir>/<build>/steve.json` (every
  release that has one, unless builds are named). The release must be extracted first; extract keeps its steve.
- **native** builds the agent for this machine's OS; **check** resolves its bindings against each named release's
  server without starting it, and prints every binding where one is named.
- **status** prints each tracked build, each platform as `supported`, `zip` (not supported) or `-` (no zip), and
  whether bedrock-data has its data.

## Agent

The blocks are what the running server says, so they are read inside it: `native/` is an agent the server loads.

- **Windows**: `bdg_inject.exe` loads `bdg_agent.dll` into `bedrock_server.exe` once it prints "Server started".
- **Linux**: `libbdg_agent.so` is preloaded (`LD_PRELOAD`), and starts once node writes `<out>/go`. Its `opendir`
  and `readdir` list every folder in NTFS's order (by name, case folded), as the Windows server's are listed: an ext4
  folder lists in an order of its filesystem's own, and the server numbers what it reads in that order (1.21.0's trial
  keys), so two machines would number them otherwise.

Before it is loaded, node adds a ticking area (`tickingarea add`), so a chunk far from the world origin stays loaded
with no player there: the agent sets blocks in it. Before 1.17.0 the console runs no command in a dimension, and the
joined client's player runs it (`execute @a`). The client is bedrock-protocol's, joining as the build's version (else
as the protocol the server advertises).

It binds what it calls for the running build (`native/bindings/`: Windows by byte patterns, slots and offsets; Linux
by the server's own symbols where it ships them, 1.16.201 to 1.21.2 but 1.17.0, else as Windows), hooks `Level::tick`,
and on the tick finds the overworld (creating it where it does not exist yet), the block type registry (every block
type and its states in runtime id order) and the item registry. Then it writes:

- `block_palette.nbt`: each state's own CompoundTag;
- `block_shapes.nbt`: each state's shapes, asked of the game: the collision boxes (at the origin and 16 blocks away),
  the visual and UI boxes, the outline, and from 1.20.30 the camera box;
- `blocks.nbt`: each block type's and state's facts (read from the BlockLegacy and each Block: numeric id, hardness,
  friction, light, fire odds, ...), what every item does to each state (`Item::getDestroySpeed`,
  `Item::canDestroySpecial`), each state's drops (the game's drop code, drawn again and again from a Random it seeds by
  the block's name, with the block set in the loaded chunk where it keeps a block actor) and map colour
  (`Block::getMapColor` with the state set in the loaded chunk, a fresh sub-chunk every 2048 states: a sub-chunk's
  palette takes no more than 4096; where a build has it inline in its callers, the parts they call, read from their
  code: the block's map colour component tinted by its tint method's sampler, else `BlockLegacy::getMapColor`);
- `items.nbt`: each registered item: its id, stack size, durability, enchant slots, tags and repair items, its name as
  the game builds it (`Item::buildDescriptionName` of a stack of one: the server's en_US name, or the key where that
  has none), and a food's legacy food component where the build keeps one;
- `enchantments.nbt`: each enchantment: its slots, levels, costs and the ones it does not go with;
- `biomes.nbt`: each biome of the registry by name with its numeric id, found among what the Level reaches;
- `effects.nbt`: each mob effect (the static `MobEffect` array, by id) with its name, language key and harm;
- `enums.nbt`: the names the game keeps for its sound events, particle types, container types, note block
  instruments, item stack request containers and map decorations, and the decoration types exploration maps mark;
- `tints.nbt`: each biome's grass, foliage and water colours, the redstone wire's in each state and the block types of
  one colour, each from the game's own colour functions (found by their code).

The console's help is asked before the agent is loaded: after 1.16.201's long draws (an unoptimized build) its console
answers no more.

What no binding names is found by content (a member: the one offset where blocks of known values read them) or by its
code and what it answers (a virtual: its slot's code, then known answers), as `game/` says of each. Each call runs
guarded, the shape calls on a worker thread with a deadline; a fault in the game fails the export, never the server's
tick. Then it writes `result.txt`, and node kills the server.

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
`LD_LIBRARY_PATH`. The client speaks RakNet through raknet-native's prebuilt binaries (patched to take another OS
release's of the same platform, as a newer kernel has none of its own: `patches/`). From 1.26.52 a server takes only
NetherNet: the client finds it on the LAN port every such server binds (UDP 7551), so those servers run one at a time.
