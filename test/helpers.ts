import { createHash } from 'node:crypto'
import { cpSync, mkdirSync, mkdtempSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { crc32, deflateRawSync } from 'node:zlib'
import nbt from 'prismarine-nbt'
import type { BlockFacts, Blocks, BlockShapes, BlockState, Items, StateFacts } from '../src/blocks.ts'
import { BEDROCK_DATA_DIR, SCHEMAS_DIR } from '../src/config.ts'
import type { ReadBlocks } from '../src/extract.ts'
import { VersionStore, type VersionJson } from '../src/versionStore.ts'
import { versionOf, zipUrl, type Channel, type Platform, type Target } from '../src/versions.ts'

/** A zip of `files` (name -> content), each deflated: as a server zip is. */
export function writeZip(file: string, files: Record<string, string | Buffer>): void {
  const locals: Buffer[] = []
  const centrals: Buffer[] = []
  let offset = 0

  for (const [name, content] of Object.entries(files)) {
    const data = Buffer.isBuffer(content) ? content : Buffer.from(content, 'utf8')
    const packed = deflateRawSync(data)
    const fileName = Buffer.from(name, 'utf8')
    const local = Buffer.alloc(30)
    local.writeUInt32LE(0x04034b50, 0)
    local.writeUInt16LE(20, 4)
    local.writeUInt16LE(8, 8)
    local.writeUInt32LE(crc32(data), 14)
    local.writeUInt32LE(packed.length, 18)
    local.writeUInt32LE(data.length, 22)
    local.writeUInt16LE(fileName.length, 26)
    const central = Buffer.alloc(46)
    central.writeUInt32LE(0x02014b50, 0)
    central.writeUInt16LE(20, 4)
    central.writeUInt16LE(20, 6)
    central.writeUInt16LE(8, 10)
    central.writeUInt32LE(crc32(data), 16)
    central.writeUInt32LE(packed.length, 20)
    central.writeUInt32LE(data.length, 24)
    central.writeUInt16LE(fileName.length, 28)
    central.writeUInt32LE(offset, 42)
    locals.push(local, fileName, packed)
    centrals.push(central, fileName)
    offset += local.length + fileName.length + packed.length
  }

  const directory = Buffer.concat(centrals)
  const end = Buffer.alloc(22)
  end.writeUInt32LE(0x06054b50, 0)
  end.writeUInt16LE(Object.keys(files).length, 8)
  end.writeUInt16LE(Object.keys(files).length, 10)
  end.writeUInt32LE(directory.length, 12)
  end.writeUInt32LE(offset, 16)
  writeFileSync(file, Buffer.concat([...locals, directory, end]))
}

export function tempDir(): string {
  return mkdtempSync(join(tmpdir(), 'bedrock-data-extractor-'))
}

/** An empty bedrock-data checkout: only the real one's schemas (BEDROCK_DATA_DIR), which every write is checked against. */
export function emptyDataRepo(): string {
  const root = tempDir()
  cpSync(join(BEDROCK_DATA_DIR, 'schemas'), join(root, 'schemas'), { recursive: true })
  return root
}

/** A vanilla en_US.lang with as many strings as a release's has (checks.ts) */
export const VANILLA = Array.from({ length: 6000 }, (_, i) => `tile.block_${i}.name=Block ${i}`).join('\n')

/**
 * A tracked release (or a build of `channel`) whose zips are already in the cache, as finished downloads leave them:
 * nothing is fetched. `zips`: each platform's vanilla en_US.lang, or every file of its zip; `supported`: the platforms
 * marked so; `wrongSha`: those tracked with another zip's sha256.
 */
export function cachedRelease(
  build: string,
  zips: Partial<Record<Platform, string | Record<string, string>>>,
  supported: Platform[] = [],
  wrongSha: Platform[] = [],
  channel: Channel = 'release',
): { store: VersionStore; cache: string } {
  const cache = tempDir()
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  const binary: VersionJson['binary'] = {}

  for (const [platform, files] of Object.entries(zips) as [Platform, string | Record<string, string>][]) {
    const zip = join(cache, channel, build, `${platform}.zip`)
    mkdirSync(dirname(zip), { recursive: true })
    writeZip(
      zip,
      typeof files === 'string' ? { 'resource_packs/vanilla/texts/en_US.lang': files, 'resource_packs/chemistry/texts/en_US.lang': 'item.element_1.name=Hydrogen\n' } : files,
    )
    const sha256 = wrongSha.includes(platform) ? 'd'.repeat(64) : createHash('sha256').update(readFileSync(zip)).digest('hex')
    binary[platform] = { url: zipUrl(build, channel, platform), sha256, size: statSync(zip).size, supported: supported.includes(platform) }
  }

  store.write({ build, version: versionOf(build, channel), channel, binary })
  return { store, cache }
}

type NbtState = BlockState['states']

/** `states` as the agent writes the palette: big-endian NBT { blocks: [{ name, states, version }] } */
export function paletteNbt(states: BlockState[]): Buffer {
  const entries = states.map((s) => ({ name: { type: 'string', value: s.name }, states: { type: 'compound', value: s.states }, version: { type: 'int', value: s.version } }))
  return nbt.writeUncompressed({ type: 'compound', name: '', value: { blocks: { type: 'list', value: { type: 'compound', value: entries } } } } as nbt.NBT, 'big')
}

/** `shapes` as the agent writes them: big-endian NBT { blocks: [{ name, states: [{ collision, visual, ui, outline, camera? }] }] } */
export function shapesNbt(shapes: BlockShapes): Buffer {
  function box(b: number[]): { type: string; value: number[] } {
    return { type: 'float', value: b }
  }
  const blocks = shapes.blocks.map((block) => ({
    name: { type: 'string', value: block.name },
    states: {
      type: 'list',
      value: {
        type: 'compound',
        value: block.states.map((s) => ({
          collision: { type: 'list', value: { type: 'list', value: s.collision.map(box) } },
          visual: { type: 'list', value: box(s.visual) },
          ui: { type: 'list', value: box(s.ui) },
          outline: { type: 'list', value: box(s.outline) },
          ...(s.camera === undefined ? {} : { camera: { type: 'list', value: { type: 'list', value: s.camera ? [box(s.camera)] : [] } } }),
        })),
      },
    },
  }))
  return nbt.writeUncompressed({ type: 'compound', name: '', value: { blocks: { type: 'list', value: { type: 'compound', value: blocks } } } } as nbt.NBT, 'big')
}

const CUBE = [0, 0, 0, 1, 1, 1]
const VERSION = 18168865

// the blocks the checks know, each with one state: its facts where the checks have them
const KNOWN: [name: string, facts: Partial<StateFacts>][] = [
  ['minecraft:air', { hardness: 0, lightDampening: 0 }],
  ['minecraft:stone', { hardness: 1.5, explosionResistance: 6, destroySpeeds: { 'minecraft:diamond_pickaxe': 8 }, harvestedBy: ['minecraft:wooden_pickaxe'] }],
  ['minecraft:dirt', { hardness: 0.5 }],
  ['minecraft:bedrock', { hardness: -1 }],
  ['minecraft:glowstone', { lightEmission: 15 }],
  ['minecraft:glass', { lightDampening: 0 }],
  ['minecraft:ice', { friction: 0.98 }],
  ['minecraft:oak_planks', { flameOdds: 5, burnOdds: 20 }],
  ['minecraft:oak_stairs', { canContainLiquid: true }],
]

function stateFacts(name: string, facts: Partial<StateFacts>): StateFacts {
  const drops: Record<string, string> = { 'minecraft:stone': 'minecraft:cobblestone', 'minecraft:dirt': 'minecraft:dirt' }
  const item = drops[name]
  return {
    data: 0,
    hardness: 1,
    explosionResistance: 1,
    friction: 0.6,
    flameOdds: 0,
    burnOdds: 0,
    lightEmission: 0,
    lightDampening: 15,
    canContainLiquid: false,
    mapColor: [0.4392157, 0.4392157, 0.4392157, 1],
    destroySpeeds: {},
    harvestedBy: [],
    drops: { samples: 64, items: item ? [{ item, aux: 0, data: 0, minCount: 1, maxCount: 1, hits: 64 }] : [] },
    ...facts,
  }
}

export function sampleItems(): Items {
  const known = [
    { name: 'minecraft:stone', maxStackSize: 64 },
    { name: 'minecraft:dirt', maxStackSize: 64 },
    { name: 'minecraft:cobblestone', maxStackSize: 64 },
    { name: 'minecraft:diamond', maxStackSize: 64 },
    { name: 'minecraft:egg', maxStackSize: 16 },
    { name: 'minecraft:bucket', maxStackSize: 16 },
    { name: 'minecraft:water_bucket', maxStackSize: 1 },
    { name: 'minecraft:bow', maxStackSize: 1, maxDamage: 384, enchantSlots: 32 },
    { name: 'minecraft:diamond_sword', maxStackSize: 1, maxDamage: 1561, enchantSlots: 16, tags: ['minecraft:is_sword'], repairItems: ['minecraft:diamond'] },
    { name: 'minecraft:diamond_pickaxe', maxStackSize: 1, maxDamage: 1561, enchantSlots: 1024 },
    { name: 'minecraft:wooden_pickaxe', maxStackSize: 1, maxDamage: 59, enchantSlots: 1024 },
    { name: 'minecraft:crafting_table', maxStackSize: 64 },
  ]
  const filler = Array.from({ length: 400 }, (_, i) => ({ name: `minecraft:item_${i}`, maxStackSize: 64 }))
  return {
    items: [...known, ...filler].map((item, id) => ({
      id,
      descriptionId: `item.${item.name.slice(10)}`,
      descriptionName: `item.${item.name.slice(10)}.name`,
      maxDamage: 0,
      enchantSlots: 0,
      tags: [],
      repairItems: [],
      componentBased: false,
      ...item,
    })),
    aliases: [{ name: 'minecraft:fish', item: 'minecraft:stone' }],
    complexAliases: [{ name: 'minecraft:log', items: ['minecraft:dirt', 'minecraft:stone'] }],
    creative: [{ item: 'minecraft:stone', metadata: 0, blockState: 1 }],
  }
}

/** A release's blocks the checks pass: the known blocks with a state each, and a block of 500 states; its shapes, facts and items. */
export function sampleBlocks(): Blocks {
  const color: NbtState[] = Array.from({ length: 500 }, (_, i) => ({ color: { type: 'int', value: i } }))
  const states: BlockState[] = [...KNOWN.map(([name]) => ({ name, states: {}, version: VERSION })), ...color.map((s) => ({ name: 'minecraft:wool', states: s, version: VERSION }))]
  const shapes: BlockShapes = {
    blocks: [
      { name: 'minecraft:air', states: [{ collision: [], visual: [0, 0, 0, 0, 0, 0], ui: [0, 0, 0, 0, 0, 0], outline: [0, 0, 0, 0, 0, 0], camera: null }] },
      ...KNOWN.slice(1).map(([name]) => ({ name, states: [{ collision: [CUBE], visual: CUBE, ui: CUBE, outline: CUBE, camera: CUBE }] })),
      {
        name: 'minecraft:wool',
        states: color.map(() => ({ collision: [[0.025, 0, 0.025, 0.975, 0.95000005, 0.975]], visual: CUBE, ui: CUBE, outline: CUBE, camera: CUBE })),
      },
    ],
  }
  const facts: BlockFacts[] = [
    ...KNOWN.map(([name, f], id) => ({
      name,
      id,
      descriptionId: `tile.${name.slice(10)}`,
      tags: [],
      requiresCorrectTool: name === 'minecraft:stone',
      translucency: name === 'minecraft:glass' ? 1 : 0,
      defaultState: 0,
      states: [stateFacts(name, f)],
    })),
    {
      name: 'minecraft:wool',
      id: KNOWN.length,
      descriptionId: 'tile.wool',
      tags: [],
      requiresCorrectTool: false,
      translucency: 0.8,
      defaultState: 0,
      loot: ['loot_tables/blocks/wool.json'],
      states: color.map(() => stateFacts('minecraft:wool', { drops: { samples: 0, items: [] } })),
    },
  ]
  return {
    palette: paletteNbt(states),
    states,
    shapes,
    blocks: { blocks: facts, lootTables: { 'loot_tables/blocks/wool.json': { pools: [] } } },
    items: sampleItems(),
    enchantments: {
      enchantments: Array.from({ length: 30 }, (_, id) => ({
        id,
        name: id ? `enchantment_${id}` : 'protection',
        descriptionId: id ? `enchantment.e${id}` : 'enchantment.protect.all',
        frequency: 10,
        tradeable: true,
        primarySlots: 0xf,
        secondarySlots: 0,
        minLevel: 1,
        maxLevel: 4,
        minCost: [1, 12, 23, 34],
        maxCost: [21, 32, 43, 54],
        incompatible: id === 1 ? [0] : [],
        treasureOnly: false,
        discoverable: true,
      })),
    },
    ...sampleSent(),
    packItems: [],
  }
}

const KNOWN_BIOMES = ['ocean', 'plains', 'desert', 'extreme_hills', 'forest', 'taiga', 'swampland', 'river', 'hell', 'the_end']
const KNOWN_EFFECTS = ['speed', 'slowness', 'haste', 'mining_fatigue', 'strength', 'instant_health', 'instant_damage', 'jump_boost', 'nausea', 'regeneration']

/** What the agent reads of the registries and the server sends a client, as the checks pass it */
function sampleSent(): Pick<Blocks, 'biomes' | 'effects' | 'enums' | 'tints' | 'recipes' | 'entities' | 'commands' | 'attributes'> {
  const biomes = [...KNOWN_BIOMES, ...Array.from({ length: 50 }, (_, i) => `biome_${i}`)].map((name, id) => ({ name, id }))
  const harmful = new Set([2, 7, 19, 25])
  const effects = Array.from({ length: 30 }, (_, i) => {
    const id = i + 1
    const name =
      id === 25 ? 'fatal_poison' : id === 26 ? 'conduit_power' : id === 29 ? 'village_hero' : id === 19 ? 'poison' : (KNOWN_EFFECTS[i] ?? `effect_${String.fromCharCode(97 + i)}`)
    return { id, name, descriptionId: `potion.${name}`, harmful: harmful.has(id) }
  })

  function names(list: string[], from = 0): { id: number; name: string }[] {
    return list.map((name, i) => ({ id: from + i, name }))
  }

  function crafting(id: number): string {
    return id ? `minecraft:item_${id}` : 'minecraft:crafting_table'
  }

  function definition(identifier: string): { description: { identifier: string } } {
    return { description: { identifier } }
  }

  return {
    biomes: {
      biomes: biomes
        .sort((a, b) => a.name.localeCompare(b.name))
        .map((b) => (b.name === 'plains' ? { ...b, definition: { temperature: 0.8, downfall: 0.4, depth: 0.125, rain: true, mapWaterColor: 0x44aff5, tags: ['plains'] } } : b)),
    },
    effects: { effects },
    enums: {
      sounds: names(['item.use.on', 'hit', 'step']),
      particles: names(['bubble'], 1),
      containers: names(['INVENTORY', 'CONTAINER', 'WORKBENCH', 'FURNACE'], -1),
      instruments: names(['note.harp', 'note.bd']),
      mapDecorations: [],
      containerNames: names(['hotbar_and_inventory', 'crafting_input', 'cursor']),
      explorationMarkers: [{ id: 15, name: 'monument' }],
    },
    tints: { biomes: [{ name: 'plains', grass: 0x91bd59, foliage: 0x77ab2f, water: 0x44aff5 }], redstone: [], constant: [] },
    recipes: {
      recipes: Array.from({ length: 1000 }, (_, id) => ({
        type: id ? ('shapeless' as const) : ('shaped' as const),
        id: `recipe_${id}`,
        block: 'crafting_table',
        ...(id ? {} : { width: 2, height: 2 }),
        input: id ? [{ item: 'minecraft:dirt', count: 1 }] : Array.from({ length: 4 }, () => ({ tag: 'minecraft:planks', count: 1 })),
        output: [{ item: crafting(id % 400), metadata: 0, count: 1, blockState: null }],
      })),
    },
    entities: {
      entities: ['player', 'zombie', 'cow', ...Array.from({ length: 100 }, (_, i) => `entity_${i}`)].map((name, id) => ({
        name: `minecraft:${name}`,
        id,
        hasSpawnEgg: id > 0,
        summonable: id > 0,
      })),
      definitions: { 'minecraft:cow': definition('minecraft:cow'), 'minecraft:player': definition('minecraft:player'), 'minecraft:zombie': definition('minecraft:zombie') },
      lootTables: { 'loot_tables/entities/zombie.json': { pools: [] } },
    },
    commands: {
      commands: ['teleport', ...Array.from({ length: 40 }, (_, i) => `command_${i}`)].map((name) => ({
        name,
        description: `commands.${name}.description`,
        aliases: name === 'teleport' ? ['tp'] : [],
        help: [`${name}:`, 'Does it.', `- /${name}`],
      })),
      enums: { GameMode: ['survival', 'creative'] },
    },
    attributes: {
      attributes: [
        { name: 'minecraft:health', min: 0, max: 20, default: 20, value: 20 },
        { name: 'minecraft:movement', min: 0, max: 3.4028235e38, default: 0.1, value: 0.1 },
      ],
    },
  }
}

type Tag = { type: string; value: unknown }

function str(value: string): Tag {
  return { type: 'string', value }
}

function int(value: number): Tag {
  return { type: 'int', value }
}

function strings(values: string[]): Tag {
  return { type: 'list', value: { type: 'string', value: values } }
}

function compounds(values: Record<string, Tag>[]): Tag {
  return { type: 'list', value: { type: 'compound', value: values } }
}

/** `blocks` as the agent writes them (blocks.nbt), the loot left out: the extractor reads that from the server's packs */
export function blocksNbt(blocks: BlockFacts[]): Buffer {
  const value = blocks.map((b) => ({
    name: str(b.name),
    id: int(b.id),
    defaultState: int(b.defaultState),
    descriptionId: str(b.descriptionId),
    requiresCorrectTool: { type: 'byte', value: b.requiresCorrectTool ? 1 : 0 },
    tags: strings(b.tags),
    translucency: { type: 'float', value: b.translucency },
    states: compounds(
      b.states.map((s) => ({
        data: int(s.data),
        hardness: { type: 'float', value: s.hardness },
        explosionResistance: { type: 'float', value: s.explosionResistance },
        friction: { type: 'float', value: s.friction },
        flameOdds: int(s.flameOdds),
        burnOdds: int(s.burnOdds),
        lightEmission: int(s.lightEmission),
        lightDampening: int(s.lightDampening),
        canContainLiquid: { type: 'byte', value: s.canContainLiquid ? 1 : 0 },
        mapColor: { type: 'list', value: { type: 'float', value: s.mapColor } },
        destroySpeeds: compounds(Object.entries(s.destroySpeeds).map(([item, speed]) => ({ item: str(item), speed: { type: 'float', value: speed } }))),
        harvestedBy: strings(s.harvestedBy),
        samples: int(s.drops.samples),
        drops: compounds(
          s.drops.items.map((d) => ({ item: str(d.item), aux: int(d.aux), data: int(d.data), minCount: int(d.minCount), maxCount: int(d.maxCount), hits: int(d.hits) })),
        ),
      })),
    ),
  }))
  return nbt.writeUncompressed({ type: 'compound', name: '', value: { blocks: compounds(value) } } as nbt.NBT, 'big')
}

/** `items` as the agent writes them (items.nbt) */
export function itemsNbt(items: Items): Buffer {
  const value = {
    items: compounds(
      items.items.map((i) => ({
        name: str(i.name),
        id: int(i.id),
        descriptionId: str(i.descriptionId),
        descriptionName: str(i.descriptionName),
        maxStackSize: int(i.maxStackSize),
        maxDamage: int(i.maxDamage),
        enchantSlots: int(i.enchantSlots),
        tags: strings(i.tags),
        repairItems: strings(i.repairItems),
      })),
    ),
    aliases: compounds(items.aliases.map((a) => ({ name: str(a.name), item: str(a.item) }))),
    complexAliases: compounds(items.complexAliases.map((a) => ({ name: str(a.name), items: strings(a.items) }))),
  }
  return nbt.writeUncompressed({ type: 'compound', name: '', value } as nbt.NBT, 'big')
}

/** What reads a build's blocks, where no server runs: `answer` for every zip (an Error: thrown). Each call is noted in `calls`. */
export function fakeBlocks(answer: Blocks | Error = sampleBlocks()): ReadBlocks & { calls: { build: string; platform: Platform }[] } {
  const calls: { build: string; platform: Platform }[] = []
  async function read(_zip: string, t: Target, platform: Platform): Promise<Blocks> {
    calls.push({ build: t.build, platform })
    await Promise.resolve()

    if (answer instanceof Error) {
      throw answer
    }

    return answer
  }
  return Object.assign(read, { calls })
}
