// The agent's files of what the game registers besides the blocks and the items: the biomes' ids, the mob effects, the
// names of some of its enums, and the colours it tints with (native/export/).
import nbt from 'prismarine-nbt'

export interface BiomeId {
  /** "plains", without minecraft: */
  name: string
  /** the numeric id a chunk's biomes are saved and sent by */
  id: number
}

export interface Effect {
  /** the id the server sends a client: speed 1, fatal_poison 25 */
  id: number
  /** "speed", "fatal_poison" */
  name: string
  /** "potion.moveSpeed"; the language has it as it is */
  descriptionId: string
  harmful: boolean
}

export interface EnumName {
  id: number
  name: string
}

export interface Enums {
  /** LevelSoundEventMap: "item.use.on" 0, "hit" 1, ...; the ids with gaps, an alias's id twice */
  sounds: EnumName[]
  /** ParticleTypeMap: "bubble" 1, ...; the ids with gaps, an alias's id twice */
  particles: EnumName[]
  /** Container::containerTypeMap: "INVENTORY" -1, "CONTAINER" 0, ... */
  containers: EnumName[]
  /** the note block instruments' sounds by instrument: "harp" or "note.harp" 0, ...; none where a build keeps no table of them */
  instruments: EnumName[]
  /** MapDecoration::Type as the game's reflection registers it: "MarkerWhite" 0, ...; none before 1.26.40 */
  mapDecorations: EnumName[]
  /** ContainerCollectionNameMap, the containers item stack requests name: "hotbar_and_inventory", "cursor", ... */
  containerNames: EnumName[]
  /** the decoration types exploration maps mark (ExplorationMapRegistry), each named by its feature: "monument" 15; in the registry's order */
  explorationMarkers: EnumName[]
}

export interface BiomeTint {
  name: string
  /** 0xRRGGBB */
  grass: number
  foliage: number
  /** its water's, where the agent reads one (from about 1.21.20 the Biome keeps it as an int it cannot tell apart) */
  water?: number
}

export interface Tints {
  biomes: BiomeTint[]
  /** the redstone wire's colour in each of its states, in state order; none from 1.21.20 */
  redstone: number[]
  /** the block types the server tints with one colour wherever they are, in runtime id order */
  constant: { name: string; color: number }[]
}

function root(raw: Buffer, file: string): Record<string, unknown> {
  const value = nbt.simplify(nbt.parseUncompressed(raw, 'big')) as unknown

  if (typeof value !== 'object' || value === null) {
    throw new Error(`${file}: no compound`)
  }

  return value as Record<string, unknown>
}

function list(o: Record<string, unknown>, key: string, file: string): Record<string, unknown>[] {
  const value = o[key]

  if (!Array.isArray(value)) {
    throw new Error(`${file}: no ${key} list`)
  }

  return value as Record<string, unknown>[]
}

export function parseBiomes(raw: Buffer): BiomeId[] {
  return list(root(raw, 'biomes.nbt'), 'biomes', 'biomes.nbt').map(({ name, id }, i) => {
    if (typeof name !== 'string' || typeof id !== 'number') {
      throw new Error(`biomes.nbt: biome ${i} is not { name, id }`)
    }

    return { name, id }
  })
}

export function parseEffects(raw: Buffer): Effect[] {
  return list(root(raw, 'effects.nbt'), 'effects', 'effects.nbt').map(({ id, name, descriptionId, harmful }, i) => {
    if (typeof id !== 'number' || typeof name !== 'string' || typeof descriptionId !== 'string' || (harmful !== 0 && harmful !== 1)) {
      throw new Error(`effects.nbt: effect ${i} is not { id, name, descriptionId, harmful }`)
    }

    return { id, name, descriptionId, harmful: harmful === 1 }
  })
}

const ENUMS = ['sounds', 'particles', 'containers', 'instruments', 'mapDecorations', 'containerNames', 'explorationMarkers'] as const

export function parseEnums(raw: Buffer): Enums {
  const o = root(raw, 'enums.nbt')
  const out = {} as Enums

  for (const key of ENUMS) {
    out[key] = list(o, key, 'enums.nbt').map(({ id, name }, i) => {
      if (typeof id !== 'number' || typeof name !== 'string') {
        throw new Error(`enums.nbt: ${key} ${i} is not { id, name }`)
      }

      return { id, name }
    })
  }

  return out
}

function isColour(n: unknown): n is number {
  return typeof n === 'number' && Number.isInteger(n) && n >= 0 && n <= 0xffffff
}

export function parseTints(raw: Buffer): Tints {
  const o = root(raw, 'tints.nbt')
  const biomes = list(o, 'biomes', 'tints.nbt').map(({ name, grass, foliage, water }, i) => {
    // water -1: none read
    if (typeof name !== 'string' || !isColour(grass) || !isColour(foliage) || (water !== -1 && !isColour(water))) {
      throw new Error(`tints.nbt: biome ${i} is not { name, grass, foliage, water }`)
    }

    return { name, grass, foliage, ...(water === -1 ? {} : { water: water as number }) }
  })
  const { redstone } = o

  if (!Array.isArray(redstone) || !redstone.every(isColour)) {
    throw new Error('tints.nbt: redstone is not a list of colours')
  }

  const constant = list(o, 'constant', 'tints.nbt').map(({ name, color }, i) => {
    if (typeof name !== 'string' || !isColour(color)) {
      throw new Error(`tints.nbt: constant ${i} is not { name, color }`)
    }

    return { name, color }
  })
  return { biomes, redstone, constant }
}
