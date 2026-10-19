import nbt from 'prismarine-nbt'
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { runAgent } from './agent.ts'
import type { Sent } from './client.ts'
import {
  checkAttributes,
  checkBiomes,
  checkBlocks,
  checkCommands,
  checkEffects,
  checkEnchantments,
  checkEntities,
  checkEnums,
  checkItems,
  checkPalette,
  checkRecipes,
  checkShapes,
  checkTints,
} from './checks.ts'
import { readEntities } from './entities.ts'
import { readBlockLoot } from './loot.ts'
import { readPackItems, type PackItem } from './packItems.ts'
import { agentHost } from './platform.ts'
import { parseBiomes, parseEffects, parseEnums, parseTints, type Effect, type Enums, type Tints } from './registries.ts'
import { biomesOf, blockStateOf, commandsOf, entitiesOf, recipesOf, type Attributes, type Biomes, type CommandsFile, type Entities, type Recipes } from './sent.ts'
import { unpackServer } from './server.ts'
import { f32 } from './util.ts'
import { compareVersions, type Platform } from './versions.ts'

export const PALETTE_FILE = 'block_palette.nbt'
export const SHAPES_FILE = 'block_shapes.json.gz'
export const BLOCKS_FILE = 'blocks.json.gz'
export const ITEMS_FILE = 'items.json.gz'
export const ENCHANTMENTS_FILE = 'enchantments.json.gz'
export const BIOMES_FILE = 'biomes.json.gz'
export const EFFECTS_FILE = 'effects.json.gz'
export const ENUMS_FILE = 'enums.json.gz'
export const TINTS_FILE = 'tints.json.gz'
export const RECIPES_FILE = 'recipes.json.gz'
export const ENTITIES_FILE = 'entities.json.gz'
export const COMMANDS_FILE = 'commands.json.gz'
export const ATTRIBUTES_FILE = 'attributes.json.gz'

export type Box = number[]

export interface StateShapes {
  collision: Box[]
  visual: Box
  ui: Box
  outline: Box
  camera?: Box | null
}

export interface BlockShapes {
  blocks: { name: string; states: StateShapes[] }[]
}

export interface BlockState {
  name: string
  states: Record<string, { type: string; value: unknown }>
  version: number
}

/** One item a state drops, over a state's `samples` draws: its stack's data values, the fewest and most in a draw, and how many draws dropped it */
export interface Drop {
  item: string
  aux: number
  data: number
  minCount: number
  maxCount: number
  hits: number
}

export interface StateFacts {
  data: number
  hardness: number
  explosionResistance: number
  friction: number
  flameOdds: number
  burnOdds: number
  lightEmission: number
  lightDampening: number
  canContainLiquid: boolean
  /** mce::Color: [r, g, b, a], each 0 - 1 */
  mapColor: number[]
  /** each item's destroy speed on the state, where it is not 1 */
  destroySpeeds: Record<string, number>
  harvestedBy: string[]
  drops: { samples: number; items: Drop[] }
}

export interface BlockFacts {
  name: string
  id: number
  descriptionId: string
  tags: string[]
  requiresCorrectTool: boolean
  translucency: number
  /** the game's default state, as an index into `states` */
  defaultState: number
  /** the loot tables its behavior pack definition names */
  loot?: string[]
  states: StateFacts[]
}

/** Every block type in runtime id order, and the loot tables they name */
export interface BlockTypes {
  blocks: BlockFacts[]
  lootTables: Record<string, unknown>
}

export interface Item {
  name: string
  id: number
  descriptionId: string
  /** Item::buildDescriptionName of a stack of one (a block item's holding its block's default state): the server's en_US name, the key where its language has none, empty where the game names it nothing */
  descriptionName: string
  maxStackSize: number
  /** 0 where it does not wear out */
  maxDamage: number
  /** Enchant::Slot bits */
  enchantSlots: number
  tags: string[]
  repairItems: string[]
  /** as the item registry the server sends has it */
  componentBased: boolean
  /** the item registry's, from 1.21.60 */
  version?: string | number
  /** the item registry's components of a component-based item (an NBT tag), from 1.21.60 */
  nbt?: unknown
  /** item_component's components of it (an NBT tag), where the server sends them (1.18.11 to 1.21.50) */
  components?: unknown
  /** a food's, as its legacy food component has it, where the build keeps one */
  food?: { nutrition: number; saturationModifier: number }
}

export interface CreativeItem {
  item: string
  metadata: number
  /** the block state it places (its runtime id: its index in the palette), null where it places none */
  blockState: number | null
  /** the stack's own NBT, where it has some */
  nbt?: unknown
}

export interface Items {
  items: Item[]
  /** an old name the game still reads, and the item it names */
  aliases: { name: string; item: string }[]
  /** an old name whose data values became items of their own, by data value */
  complexAliases: { name: string; items: string[] }[]
  creative: CreativeItem[]
}

export interface Enchantment {
  /** Enchant::Type: the id an item's ench list names it by */
  id: number
  name: string
  descriptionId: string
  /** its weight among those an enchanting table offers */
  frequency: number
  tradeable: boolean
  /** Enchant::Slot bits of the items an enchanting table offers it for */
  primarySlots: number
  /** ... and of those an anvil puts it on */
  secondarySlots: number
  minLevel: number
  maxLevel: number
  /** at levels 1 to its max level (at least 2) */
  minCost: number[]
  maxCost: number[]
  incompatible: number[]
  treasureOnly: boolean
  discoverable: boolean
}

export interface Enchantments {
  enchantments: Enchantment[]
}

export interface Blocks {
  palette: Buffer
  states: BlockState[]
  shapes: BlockShapes
  blocks: BlockTypes
  items: Items
  enchantments: Enchantments
  biomes: Biomes
  effects: { effects: Effect[] }
  enums: Enums
  tints: Tints
  recipes: Recipes
  entities: Entities
  commands: CommandsFile
  attributes: Attributes
  /** the items the packs define, by the pack the server numbers them in (not written) */
  packItems: PackItem[]
}

interface Tag<T = unknown> {
  type: string
  value: T
}

export function parsePalette(raw: Buffer): BlockState[] {
  const root = nbt.parseUncompressed(raw, 'big') as Tag<{ blocks?: Tag<Tag<Partial<Record<'name' | 'states' | 'version', Tag>>[]>> }>
  const blocks = root.value.blocks

  if (blocks?.type !== 'list' || blocks.value.type !== 'compound') {
    throw new Error(`${PALETTE_FILE}: no blocks list of compounds`)
  }

  return blocks.value.value.map(({ name, states, version }, i) => {
    if (name?.type !== 'string' || states?.type !== 'compound' || version?.type !== 'int') {
      throw new Error(`${PALETTE_FILE}: state ${i} is not { name, states, version }`)
    }

    return {
      name: name.value as string,
      states: states.value as BlockState['states'],
      version: version.value as number,
    }
  })
}

function box(value: unknown, what: string): Box {
  if (!Array.isArray(value) || value.length !== 6 || !value.every((c) => typeof c === 'number')) {
    throw new Error(`${what}: not 6 numbers`)
  }

  return value.map(f32)
}

// The Windows server is built with MSVC up to 1.26.10, with clang (the Linux server's compiler) from 1.26.20
const WINDOWS_CLANG_FROM = '1.26.20'
// the chests' top as each compiler folds ChestBlock's box: clang, MSVC
const CLANG_CHEST_TOP = 0.95000005
const MSVC_CHEST_TOP = 0.95

/**
 * The shapes a Linux server gives, as the release's Windows server gives them. Their collision boxes differ only in the
 * chests' (chest, ender chest, trapped chest, the copper chests: each of their shapes is that one box), whose top
 * ChestBlock's constructor gives as a constant that clang folds to 0.95000005 and MSVC to 0.95. From 1.26.20 both
 * servers are built with clang and give the same.
 */
export function shapesAsOnWindows(shapes: BlockShapes, build: string, platform: Platform): BlockShapes {
  if (platform !== 'linux' || compareVersions(build, WINDOWS_CLANG_FROM) >= 0) {
    return shapes
  }

  function box(b: Box): Box {
    return b[4] === CLANG_CHEST_TOP ? b.with(4, MSVC_CHEST_TOP) : b
  }

  return {
    blocks: shapes.blocks.map(({ name, states }) => ({
      name,
      states: states.map(({ collision, visual, ui, outline, camera }) => ({
        collision: collision.map(box),
        visual: box(visual),
        ui: box(ui),
        outline: box(outline),
        ...(camera === undefined ? {} : { camera: camera && box(camera) }),
      })),
    })),
  }
}

// from it a state's explosion resistance is the game's quotient of its type's (which keeps it times 5) by 5
const RESISTANCE_QUOTIENT_FROM = '1.21.42'

/**
 * The block facts a Linux server gives, as the release's Windows server gives them. From 1.21.42 to 1.26.10 they differ
 * in the explosion resistance of the states whose quotient by 5 rounds otherwise when MSVC multiplies by 0.2 (21 / 5 is
 * 4.2, 21 × 0.2 is 4.2000003): terracotta, basalt, concrete, the dirt path.
 */
export function factsAsOnWindows(blocks: BlockTypes, build: string, platform: Platform): BlockTypes {
  if (platform !== 'linux' || compareVersions(build, RESISTANCE_QUOTIENT_FROM) < 0 || compareVersions(build, WINDOWS_CLANG_FROM) >= 0) {
    return blocks
  }

  return {
    ...blocks,
    blocks: blocks.blocks.map((b) => ({
      ...b,
      states: b.states.map((s) => ({ ...s, explosionResistance: f32(Math.fround(Math.fround(s.explosionResistance * 5) * Math.fround(0.2))) })),
    })),
  }
}

// from it the Linux server, too, numbers a pack's items in their files' name order
const PACK_ITEMS_BY_NAME_FROM = '1.26.40'

/** As NTFS orders a folder: ASCII letters compared upper-cased ('_' after them) */
function ntfsOrder(a: string, b: string): number {
  function upper(s: string): string {
    return s.replace(/[a-z]/g, (c) => c.toUpperCase())
  }

  return upper(a) < upper(b) ? -1 : upper(a) > upper(b) ? 1 : 0
}

/**
 * The items a Linux server gives, as the release's Windows server gives them. Both servers number the items a pack
 * defines (packItems) on the same ids, the Windows server in the order its NTFS folder lists their files, the Linux
 * server before 1.26.40 in an order of its own (from 1.21.42 the same on every machine, before it its ext4 folder's).
 */
export function itemsAsOnWindows(items: Items, packItems: PackItem[], build: string, platform: Platform): Items {
  if (platform !== 'linux' || compareVersions(build, PACK_ITEMS_BY_NAME_FROM) >= 0) {
    return items
  }

  const idOf = new Map(items.items.map((i) => [i.name, i.id]))
  const byPack = new Map<string, PackItem[]>()

  for (const item of packItems) {
    if (idOf.has(item.name)) {
      byPack.set(item.pack, [...(byPack.get(item.pack) ?? []), item])
    }
  }

  const ids = new Map<string, number>()

  for (const inPack of byPack.values()) {
    const taken = inPack.map((i) => idOf.get(i.name) ?? 0).sort((a, b) => a - b)
    inPack.sort((a, b) => ntfsOrder(a.file, b.file)).forEach((i, k) => ids.set(i.name, taken[k]))
  }

  return { ...items, items: items.items.map((i) => ({ ...i, id: ids.get(i.name) ?? i.id })).sort((a, b) => a.id - b.id) }
}

/** What a Linux server gives, as the release's Windows server gives it, where only their compilers and filesystems make them differ */
export function asOnWindows(read: Blocks, build: string, platform: Platform): Blocks {
  return {
    ...read,
    shapes: shapesAsOnWindows(read.shapes, build, platform),
    blocks: factsAsOnWindows(read.blocks, build, platform),
    items: itemsAsOnWindows(read.items, read.packItems, build, platform),
  }
}

export function parseShapes(raw: Buffer): BlockShapes {
  const root = nbt.simplify(nbt.parseUncompressed(raw, 'big')) as { blocks?: unknown }

  if (!Array.isArray(root.blocks)) {
    throw new Error('block_shapes.nbt: no blocks list')
  }

  return {
    blocks: (root.blocks as { name?: unknown; states?: unknown }[]).map(({ name, states }, i) => {
      if (typeof name !== 'string' || !Array.isArray(states)) {
        throw new Error(`block_shapes.nbt: block ${i} is not { name, states }`)
      }

      return {
        name,
        states: (states as { collision?: unknown; visual?: unknown; ui?: unknown; outline?: unknown; camera?: unknown }[]).map(({ collision, visual, ui, outline, camera }, s) => {
          const at = `${name} #${s}`

          if (!Array.isArray(collision)) {
            throw new Error(`${at}: no collision boxes`)
          }

          const shapes: StateShapes = {
            collision: collision.map((b, n) => box(b, `${at} collision ${n}`)),
            visual: box(visual, `${at} visual`),
            ui: box(ui, `${at} ui`),
            outline: box(outline, `${at} outline`),
          }

          if (camera !== undefined) {
            if (!Array.isArray(camera) || camera.length > 1) {
              throw new Error(`${at}: camera is not none or one box`)
            }

            shapes.camera = camera.length ? box(camera[0], `${at} camera`) : null
          }

          return shapes
        }),
      }
    }),
  }
}

function isNumber(value: unknown): value is number {
  return typeof value === 'number' && Number.isFinite(value)
}

function numbers<K extends string>(o: Record<string, unknown>, keys: readonly K[], at: string): Record<K, number> {
  const out = {} as Record<K, number>

  for (const key of keys) {
    const value = o[key]

    if (!isNumber(value)) {
      throw new Error(`${at}: no ${key}`)
    }

    out[key] = value
  }

  return out
}

function strings(value: unknown, at: string): string[] {
  if (!Array.isArray(value) || !value.every((v) => typeof v === 'string')) {
    throw new Error(`${at}: not a list of strings`)
  }

  return value as string[]
}

function mapColorOf(value: unknown, at: string): number[] {
  if (!Array.isArray(value) || value.length !== 4 || !value.every(isNumber) || !value.every((c) => c >= 0 && c <= 1)) {
    throw new Error(`${at}: no map colour of four parts 0 - 1`)
  }

  return (value as number[]).map(f32)
}

const STATE_FACTS = ['data', 'hardness', 'explosionResistance', 'friction', 'flameOdds', 'burnOdds', 'lightEmission', 'lightDampening', 'canContainLiquid'] as const
const DROP_NUMBERS = ['aux', 'data', 'minCount', 'maxCount', 'hits'] as const

function parseState(o: Record<string, unknown>, at: string): StateFacts {
  const { canContainLiquid, ...facts } = numbers(o, STATE_FACTS, at)
  const { destroySpeeds, harvestedBy, samples, drops } = o

  if (!Array.isArray(destroySpeeds) || !Array.isArray(drops) || !isNumber(samples)) {
    throw new Error(`${at}: no destroySpeeds, samples or drops`)
  }

  return {
    ...facts,
    hardness: f32(facts.hardness),
    explosionResistance: f32(facts.explosionResistance),
    friction: f32(facts.friction),
    canContainLiquid: canContainLiquid === 1,
    mapColor: mapColorOf(o.mapColor, at),
    destroySpeeds: Object.fromEntries(
      (destroySpeeds as Record<string, unknown>[]).map(({ item, speed }) => {
        if (typeof item !== 'string' || !isNumber(speed)) {
          throw new Error(`${at}: a destroy speed is not { item, speed }`)
        }

        return [item, f32(speed)]
      }),
    ),
    harvestedBy: strings(harvestedBy, `${at} harvestedBy`),
    drops: {
      samples,
      items: (drops as Record<string, unknown>[]).map((d) => {
        if (typeof d.item !== 'string') {
          throw new Error(`${at}: a drop without its item`)
        }

        return { item: d.item, ...numbers(d, DROP_NUMBERS, `${at} ${d.item}`) }
      }),
    },
  }
}

export function parseBlockTypes(raw: Buffer): BlockFacts[] {
  const root = nbt.simplify(nbt.parseUncompressed(raw, 'big')) as { blocks?: unknown }

  if (!Array.isArray(root.blocks)) {
    throw new Error('blocks.nbt: no blocks list')
  }

  return (root.blocks as Record<string, unknown>[]).map((b, i) => {
    const { name, descriptionId, tags, states } = b

    if (typeof name !== 'string' || typeof descriptionId !== 'string' || !Array.isArray(states)) {
      throw new Error(`blocks.nbt: block ${i} is not { name, descriptionId, states, ... }`)
    }

    const n = numbers(b, ['id', 'requiresCorrectTool', 'translucency', 'defaultState'] as const, name)

    return {
      name,
      id: n.id,
      descriptionId,
      tags: strings(tags, `${name} tags`),
      requiresCorrectTool: n.requiresCorrectTool === 1,
      translucency: f32(n.translucency),
      defaultState: n.defaultState,
      states: (states as Record<string, unknown>[]).map((s, at) => parseState(s, `${name} #${at}`)),
    }
  })
}

export function parseItems(raw: Buffer): Items {
  const root = nbt.simplify(nbt.parseUncompressed(raw, 'big')) as { items?: unknown }

  if (!Array.isArray(root.items)) {
    throw new Error('items.nbt: no items list')
  }

  const root2 = root as { items: Record<string, unknown>[]; aliases?: unknown; complexAliases?: unknown }
  const items = root2.items.map((o, i) => {
    const { name, descriptionId, descriptionName } = o

    if (typeof name !== 'string' || typeof descriptionId !== 'string' || typeof descriptionName !== 'string') {
      throw new Error(`items.nbt: item ${i} is not { name, id, descriptionId, descriptionName, ... }`)
    }

    const n = numbers(o, ['id', 'maxStackSize', 'maxDamage', 'enchantSlots'] as const, name)
    const { nutrition, saturationModifier } = o
    const item: Item = {
      name,
      id: n.id,
      descriptionId,
      descriptionName,
      maxStackSize: n.maxStackSize,
      maxDamage: n.maxDamage,
      enchantSlots: n.enchantSlots,
      tags: strings(o.tags, `${name} tags`),
      repairItems: strings(o.repairItems, `${name} repairItems`),
      componentBased: false,
    }

    if (isNumber(nutrition) && isNumber(saturationModifier)) {
      item.food = { nutrition, saturationModifier: f32(saturationModifier) }
    }

    return item
  })

  if (!Array.isArray(root2.aliases) || !Array.isArray(root2.complexAliases)) {
    throw new Error('items.nbt: no aliases or complexAliases')
  }

  const aliases = (root2.aliases as Record<string, unknown>[]).map(({ name, item }) => {
    if (typeof name !== 'string' || typeof item !== 'string') {
      throw new Error('items.nbt: an alias is not { name, item }')
    }

    return { name, item }
  })
  const complexAliases = (root2.complexAliases as Record<string, unknown>[]).map((a) => {
    if (typeof a.name !== 'string') {
      throw new Error('items.nbt: a complex alias without its name')
    }

    return { name: a.name, items: strings(a.items, `${a.name} items`) }
  })

  return { items: items.sort((a, b) => a.id - b.id), aliases, complexAliases, creative: [] }
}

function ints(value: unknown, at: string): number[] {
  if (!Array.isArray(value) || !value.every(isNumber)) {
    throw new Error(`${at}: not a list of numbers`)
  }

  return value as number[]
}

export function parseEnchantments(raw: Buffer): Enchantments {
  const root = nbt.simplify(nbt.parseUncompressed(raw, 'big')) as { enchantments?: unknown }

  if (!Array.isArray(root.enchantments)) {
    throw new Error('enchantments.nbt: no enchantments list')
  }

  return {
    enchantments: (root.enchantments as Record<string, unknown>[]).map((e, i) => {
      const { name, descriptionId } = e

      if (typeof name !== 'string' || typeof descriptionId !== 'string') {
        throw new Error(`enchantments.nbt: enchantment ${i} is not { id, name, descriptionId, ... }`)
      }

      const n = numbers(e, ['id', 'frequency', 'tradeable', 'primarySlots', 'secondarySlots', 'minLevel', 'maxLevel', 'treasureOnly', 'discoverable'] as const, name)
      return {
        id: n.id,
        name,
        descriptionId,
        frequency: n.frequency,
        tradeable: n.tradeable === 1,
        primarySlots: n.primarySlots,
        secondarySlots: n.secondarySlots,
        minLevel: n.minLevel,
        maxLevel: n.maxLevel,
        minCost: ints(e.minCost, `${name} minCost`),
        maxCost: ints(e.maxCost, `${name} maxCost`),
        incompatible: ints(e.incompatible, `${name} incompatible`),
        treasureOnly: n.treasureOnly === 1,
        discoverable: n.discoverable === 1,
      }
    }),
  }
}

/**
 * A state's network hash, what packets name it by from 1.19.80: FNV-1a (32 bits) of its name and properties, sorted, as
 * a little-endian NBT compound with no name.
 */
export function stateHash(state: BlockState): number {
  const properties = Object.fromEntries(
    Object.keys(state.states)
      .sort()
      .map((k) => [k, state.states[k]]),
  )
  const root = { type: 'compound', name: '', value: { name: { type: 'string', value: state.name }, states: { type: 'compound', value: properties } } } as nbt.NBT
  let hash = 0x811c9dc5

  for (const byte of nbt.writeUncompressed(root, 'little')) {
    hash = Math.imul(hash ^ byte, 0x01000193) >>> 0
  }

  return hash
}

/** `items` with what the server sent a client of them: the item registry's (ids checked), item_component's, and the creative list. */
export function withSent(items: Items, sent: Sent, stateOf: (block: number, what: string) => number | null): Items {
  const registry = new Map(sent.itemStates.map((s) => [s.name, s]))
  const names = new Map(sent.itemStates.map((s) => [s.runtime_id, s.name]))
  const components = sent.itemComponents ?? new Map<string, unknown>()

  if (registry.size !== items.items.length) {
    throw new Error(`the server sends ${registry.size} items, the registry has ${items.items.length}`)
  }

  const unknown = [...components.keys()].find((name) => !registry.has(name))

  if (unknown) {
    throw new Error(`item_component names ${unknown}, which the registry does not have`)
  }

  const withRegistry = items.items.map((item) => {
    const state = registry.get(item.name)

    if (state?.runtime_id !== item.id) {
      throw new Error(`${item.name}: id ${item.id}, the server sends ${state?.runtime_id}`)
    }

    return {
      ...item,
      componentBased: state.component_based,
      ...(state.version === undefined ? {} : { version: state.version }),
      ...(state.nbt === undefined ? {} : { nbt: state.nbt }),
      ...(components.has(item.name) ? { components: components.get(item.name) } : {}),
    }
  })
  const creative = sent.creative.map((c, i) => {
    const item = names.get(c.id)

    if (item === undefined) {
      throw new Error(`creative item ${i}: no item of id ${c.id}`)
    }

    return { item, metadata: c.metadata, blockState: stateOf(c.block, `creative item ${i} (${item})`), ...(c.nbt === undefined ? {} : { nbt: c.nbt }) }
  })
  return { ...items, items: withRegistry, creative }
}

/** The agent's files in `out`, with what the behavior packs in `serverDir` define, what the server sent and its console's help; checked. */
export function readAgentOutput(out: string, serverDir: string, sent: Sent, help: Record<string, string[]>): Blocks {
  const palette = readFileSync(join(out, PALETTE_FILE))
  const states = parsePalette(palette)
  const shapes = parseShapes(readFileSync(join(out, 'block_shapes.nbt')))
  const loot = readBlockLoot(serverDir)
  const facts = parseBlockTypes(readFileSync(join(out, 'blocks.nbt'))).map((b) => (loot.blocks[b.name] ? { ...b, loot: loot.blocks[b.name] } : b))
  const blocks: BlockTypes = { blocks: facts, lootTables: loot.tables }
  const stateOf = blockStateOf(sent, states, stateHash)
  const items = withSent(parseItems(readFileSync(join(out, 'items.nbt'))), sent, stateOf)
  const enchantments = parseEnchantments(readFileSync(join(out, 'enchantments.nbt')))
  const biomes = biomesOf(sent, parseBiomes(readFileSync(join(out, 'biomes.nbt'))))
  const effects = { effects: parseEffects(readFileSync(join(out, 'effects.nbt'))) }
  const enums = parseEnums(readFileSync(join(out, 'enums.nbt')))
  const tints = parseTints(readFileSync(join(out, 'tints.nbt')))
  const recipes = recipesOf(sent, stateOf)
  const entities = entitiesOf(sent, readEntities(serverDir))
  const commands = commandsOf(sent, help)
  const attributes = { attributes: sent.attributes }
  const packItems = readPackItems(serverDir)
  checkPalette(states)
  checkShapes(shapes, states)
  checkItems(items)
  checkBlocks(blocks, states, items)
  checkEnchantments(enchantments)
  checkBiomes(biomes)
  checkEffects(effects.effects)
  checkEnums(enums)
  checkTints(tints, biomes, states)
  checkRecipes(recipes, items)
  checkEntities(entities)
  checkCommands(commands)
  checkAttributes(attributes)
  return { palette, states, shapes, blocks, items, enchantments, biomes, effects, enums, tints, recipes, entities, commands, attributes, packItems }
}

export async function readBlocks(zip: string, build: string, platform: Platform, work: string, signal?: AbortSignal): Promise<Blocks> {
  const host = agentHost(platform)
  const server = join(work, 'server')
  const out = join(work, 'out')
  signal?.throwIfAborted()
  await unpackServer(zip, server, host)
  const { sent, help } = await runAgent(server, build, host, out, signal)
  return readAgentOutput(out, server, sent, help)
}
