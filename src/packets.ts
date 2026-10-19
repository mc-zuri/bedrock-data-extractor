// What the server sends a joining client, as bedrock-protocol decodes it, in bedrock-data's form: the player's
// attributes, the biome definitions, the entity identifiers, the crafting recipes, the commands and each item's
// components. Each packet's form changes over the builds; each function reads every form the supported builds send. A
// float is written as the shortest decimal of its float32.
import nbt from 'prismarine-nbt'
import { f32 } from './util.ts'

type Params = Record<string, unknown>

export interface Attribute {
  /** "minecraft:movement", "minecraft:player.hunger" */
  name: string
  min: number
  max: number
  default: number
  value: number
}

export interface BiomeDefinition {
  /** "plains", without minecraft: */
  name: string
  temperature: number
  downfall: number
  depth?: number
  scale?: number
  /** whether it rains (or snows) there */
  rain?: boolean
  /** its water's colour on a map, 0xRRGGBB */
  mapWaterColor?: number
  tags: string[]
}

export interface EntityIdentifier {
  /** "minecraft:zombie" */
  name: string
  /** its runtime id: the numeric entity type */
  id: number
  hasSpawnEgg: boolean
  summonable: boolean
}

/** What a crafting ingredient takes, and how many; `metadata` absent: any data value. */
export type Ingredient = ({ item: string; metadata?: number } | { descriptor: string; metadata?: number } | { tag: string } | { alias: string } | { molang: string }) & {
  count: number
}

export interface RecipeOutput {
  item: string
  metadata: number
  count: number
  /** the block state it carries, as the server names it (a runtime id, or from 1.19.80 a state's network hash); 0: none */
  block: number
}

export interface Recipe {
  type: 'shaped' | 'shapeless' | 'shulker_box'
  id: string
  /** the block it is made at: crafting_table, stonecutter, ...; deprecated, the crafting grid's old recipes */
  block: string
  /** a shaped recipe's grid */
  width?: number
  height?: number
  /** a shaped recipe's slots row by row (null: an empty one); a shapeless recipe's ingredients */
  input: (Ingredient | null)[]
  /** what making it gives: the result, then what the ingredients give back (cake's buckets) */
  output: RecipeOutput[]
}

export interface Command {
  name: string
  description: string
  aliases: string[]
}

export interface Commands {
  commands: Command[]
  /** every enum a parameter may take, by name: its values, sorted (a soft enum's as the server fills them when a client joins) */
  enums: Record<string, string[]>
}

const ANY_DATA = 32767
const RECIPE_TYPES = new Set(['shaped', 'shapeless', 'shulker_box'])

function namespaced(name: string): string {
  return name.includes(':') ? name : `minecraft:${name}`
}

function fail(what: string): never {
  throw new Error(`${what}: not as the builds send it`)
}

/** update_attributes: the attributes of the entity it is for, in the server's order. */
export function attributesOf(params: Params): Attribute[] {
  const { attributes } = params

  if (!Array.isArray(attributes)) {
    fail('update_attributes')
  }

  return (attributes as Record<string, unknown>[]).map((a) => {
    const { name, min, max, current } = a

    if (typeof name !== 'string' || typeof min !== 'number' || typeof max !== 'number' || typeof a.default !== 'number' || typeof current !== 'number') {
      fail(`update_attributes ${String(name)}`)
    }

    return { name, min: f32(min), max: f32(max), default: f32(a.default), value: f32(current) }
  })
}

interface NbtBiome {
  temperature?: number
  downfall?: number
  depth?: number
  scale?: number
  rain?: number
  waterColorR?: number
  waterColorG?: number
  waterColorB?: number
  tags?: string[]
}

interface ListedBiome {
  name_index: number
  temperature: number
  downfall: number
  depth?: number
  scale?: number
  rain?: boolean
  map_water_colour?: number
  tags?: (number | string)[] | null
}

function rgb(r: number | undefined, g: number | undefined, b: number | undefined): number | undefined {
  if (r === undefined || g === undefined || b === undefined) {
    return undefined
  }

  return (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255)
}

/**
 * biome_definition_list: every biome, in the server's order. Until 1.21.70 an NBT compound of definitions by name; from
 * 1.21.80 a list naming its biomes and tags by their index in a list of strings, its water colour one ARGB integer.
 */
export function biomeDefinitionsOf(params: Params): BiomeDefinition[] {
  if (params.nbt) {
    const defs = nbt.simplify(params.nbt as nbt.Tags[nbt.TagType]) as Record<string, NbtBiome>
    return Object.entries(defs).map(([name, d]) => {
      if (typeof d.temperature !== 'number' || typeof d.downfall !== 'number') {
        fail(`biome ${name}`)
      }

      return {
        name: name.replace(/^minecraft:/, ''),
        temperature: f32(d.temperature),
        downfall: f32(d.downfall),
        ...(d.depth === undefined ? {} : { depth: f32(d.depth) }),
        ...(d.scale === undefined ? {} : { scale: f32(d.scale) }),
        ...(d.rain === undefined ? {} : { rain: d.rain !== 0 }),
        ...(rgb(d.waterColorR, d.waterColorG, d.waterColorB) === undefined ? {} : { mapWaterColor: rgb(d.waterColorR, d.waterColorG, d.waterColorB) }),
        tags: d.tags ?? [],
      }
    })
  }

  const strings = params.string_list
  const definitions = params.biome_definitions

  if (!Array.isArray(strings) || !Array.isArray(definitions)) {
    fail('biome_definition_list')
  }

  return (definitions as ListedBiome[]).map((d) => ({
    name: (strings as string[])[d.name_index].replace(/^minecraft:/, ''),
    temperature: f32(d.temperature),
    downfall: f32(d.downfall),
    ...(d.depth === undefined ? {} : { depth: f32(d.depth) }),
    ...(d.scale === undefined ? {} : { scale: f32(d.scale) }),
    ...(d.rain === undefined ? {} : { rain: d.rain }),
    ...(d.map_water_colour === undefined ? {} : { mapWaterColor: d.map_water_colour & 0xffffff }),
    tags: (d.tags ?? []).map((tag) => (typeof tag === 'number' ? (strings as string[])[tag] : tag)),
  }))
}

/** available_entity_identifiers: every entity a client may be sent, in the server's order. */
export function entityIdentifiersOf(params: Params): EntityIdentifier[] {
  const list = (nbt.simplify(params.nbt as nbt.Tags[nbt.TagType]) as { idlist?: unknown }).idlist

  if (!Array.isArray(list)) {
    fail('available_entity_identifiers')
  }

  return (list as Record<string, unknown>[]).map((e) => {
    if (typeof e.id !== 'string' || typeof e.rid !== 'number') {
      fail(`available_entity_identifiers ${String(e.id)}`)
    }

    return { name: e.id, id: e.rid, hasSpawnEgg: e.hasspawnegg === 1, summonable: e.summonable === 1 }
  })
}

type Raw = Record<string, unknown>

/**
 * crafting_data's recipes of a grid (shaped, shapeless, and shulker box ones that keep the box's contents) at every block,
 * in the server's order. Until 1.26.30 one list of { type, recipe }, from 1.26.40 a list per type; an ingredient by
 * network id until 1.26.30 (with a complex alias or item tag from 1.19.40), by name from 1.26.40.
 */
export function recipesOf(params: Params, itemNames: Map<number, string>): Recipe[] {
  function nameOf(id: unknown, what: string): string {
    const name = typeof id === 'number' ? itemNames.get(id) : undefined

    if (name === undefined) {
      throw new Error(`${what}: no item of network id ${String(id)}`)
    }

    return name
  }

  function data(m: unknown): { metadata?: number } {
    return typeof m !== 'number' || m === ANY_DATA ? {} : { metadata: m }
  }

  function ingredient(i: Raw | null | undefined, what: string): Ingredient | null {
    if (!i || i.type === 'invalid' || (i.type === undefined && !i.network_id)) {
      return null
    }

    const count = typeof i.count === 'number' ? i.count : 1
    const kind = i.type === 'valid' ? i.descriptor_type : (i.type ?? 'int_id_meta')

    switch (kind) {
      // 1.16.201 - 1.19.21: { network_id, network_data }
      case 'int_id_meta':
        return { item: nameOf(i.network_id, what), ...data(i.metadata ?? i.network_data), count }
      case 'name':
        return { descriptor: namespaced(String(i.name)), ...data(i.metadata), count }
      case 'string_id_meta':
        return { item: namespaced(String(i.name)), ...data(i.metadata), count }
      case 'item_tag':
        return { tag: String(i.tag), count }
      case 'complex_alias':
        return { alias: namespaced(String(i.name)), count }
      case 'molang':
        return { molang: String(i.expression), count }
      default:
        throw new Error(`${what}: ingredient type ${String(kind)}`)
    }
  }

  // { network_id, count, metadata, block_runtime_id }, or 1.16.201's { network_id, auxiliary_value: data << 8 | count }
  function output(o: Raw, what: string): RecipeOutput {
    const legacy = typeof o.auxiliary_value === 'number'
    const metadata = legacy ? (o.auxiliary_value as number) >> 8 : typeof o.metadata === 'number' ? o.metadata : 0
    return {
      item: nameOf(o.network_id, what),
      metadata: metadata === ANY_DATA ? 0 : metadata,
      count: legacy ? (o.auxiliary_value as number) & 0xff : typeof o.count === 'number' ? o.count : 1,
      block: typeof o.block_runtime_id === 'number' ? o.block_runtime_id >>> 0 : 0,
    }
  }

  const listed = Array.isArray(params.recipes)
    ? (params.recipes as { type: string; recipe: Raw }[])
    : [...RECIPE_TYPES].flatMap((type) => ((params[`${type}_recipes`] as Raw[] | undefined) ?? []).map((recipe) => ({ type, recipe })))
  return listed
    .filter(({ type }) => RECIPE_TYPES.has(type))
    .map(({ type, recipe }) => {
      const what = `${type} ${String(recipe.recipe_id)}`
      const input = (Array.isArray(recipe.input) ? (recipe.input as unknown[]).flat() : []) as (Raw | null)[]
      const shaped = type === 'shaped'

      if (shaped && input.length !== (recipe.width as number) * (recipe.height as number)) {
        throw new Error(`${what}: ${input.length} slots for ${String(recipe.width)} x ${String(recipe.height)}`)
      }

      return {
        type: type as Recipe['type'],
        id: String(recipe.recipe_id),
        block: String(recipe.block),
        ...(shaped ? { width: recipe.width as number, height: recipe.height as number } : {}),
        input: input.map((i, n) => ingredient(i, `${what} slot ${n}`)),
        output: (recipe.output as Raw[]).map((o) => output(o, what)),
      }
    })
}

/**
 * available_commands: every command, in the server's order, and every enum a parameter may take by name, its values: the
 * enums, the soft (dynamic) ones, and execute's subcommands that chain into others (ExecuteChainedOption_0: as, at, ...).
 * Each enum's values are sorted: the server lists those it reads from its packs' files (Features, CameraPresets) in the
 * order the machine lists the files, and a value is sent by its text, so the order says nothing.
 */
export function commandsOf(params: Params): Commands {
  const values = params.enum_values as string[]
  const enumList = params.enums as { name: string; values: number[] }[]
  const commandData = params.command_data as { name: string; description: string; alias: number }[]

  if (!Array.isArray(values) || !Array.isArray(enumList) || !Array.isArray(commandData)) {
    fail('available_commands')
  }

  const enums: Record<string, string[]> = {}

  for (const e of enumList) {
    enums[e.name] = e.values.map((i) => values[i])
  }

  for (const e of (params.dynamic_enums ?? []) as { name: string; values: string[] }[]) {
    enums[e.name] = e.values
  }

  const chained = (params.chained_subcommand_values ?? []) as string[]

  for (const e of (params.chained_subcommands ?? []) as { name: string; values: { index: number }[] }[]) {
    enums[e.name] = e.values.map((v) => chained[v.index])
  }

  for (const list of Object.values(enums)) {
    list.sort()
  }

  const commands = commandData.map(({ name, description, alias }) => ({
    name,
    description,
    aliases: alias >= 0 ? enumList[alias].values.map((i) => values[i]).filter((a) => a !== name) : [],
  }))
  return { commands, enums }
}

/** item_component: each item's components as an NBT tag, by name (from 1.18.11 the vanilla items', foods first). */
export function itemComponentsOf(params: Params): Map<string, unknown> {
  const { entries } = params

  if (!Array.isArray(entries) || !entries.every((e: { name?: unknown }) => typeof e.name === 'string')) {
    fail('item_component')
  }

  return new Map((entries as { name: string; nbt: unknown }[]).map((e) => [e.name, e.nbt]))
}
