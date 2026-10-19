// bedrock-data's files of what the server sends a joining client, with what the game says of the same (the agent's
// biome ids) and what its console says (the commands' help): the recipes, the entities, the commands, the player's
// attributes and the biomes.
import type { BlockState } from './blocks.ts'
import type { Sent } from './client.ts'
import type { EntityDefinitions } from './entities.ts'
import type { Attribute, BiomeDefinition, Command, EntityIdentifier, Ingredient, Recipe } from './packets.ts'
import type { BiomeId } from './registries.ts'

export interface RecipeStack {
  item: string
  metadata: number
  count: number
  /** the block state it carries (its runtime id), null where none */
  blockState: number | null
}

export interface Recipes {
  recipes: (Omit<Recipe, 'output'> & { input: (Ingredient | null)[]; output: RecipeStack[] })[]
}

export interface Entities {
  entities: EntityIdentifier[]
  definitions: EntityDefinitions['definitions']
  lootTables: EntityDefinitions['lootTables']
}

export interface CommandsFile {
  commands: (Command & { help: string[] })[]
  enums: Record<string, string[]>
}

export interface Attributes {
  attributes: Attribute[]
}

export interface Biomes {
  biomes: (BiomeId & { definition?: Omit<BiomeDefinition, 'name'> })[]
}

/**
 * The block state a packet names, by its runtime id, or from 1.19.80 its network hash (as the creative list and the
 * recipes name them all); 0 is none.
 */
export function blockStateOf(sent: Sent, states: BlockState[], hash: (state: BlockState) => number): (block: number, what: string) => number | null {
  const named = [...sent.creative.map((c) => c.block), ...sent.recipes.flatMap((r) => r.output.map((o) => o.block))].filter((b) => b !== 0)
  const byHash = new Map(states.map((s, i) => [hash(s), i]))
  const hashes = named.length > 0 && named.every((b) => byHash.has(b))
  return (block, what) => {
    if (block === 0) {
      return null
    }

    const state = hashes ? byHash.get(block) : block

    if (state === undefined || state >= states.length) {
      throw new Error(`${what}: block ${block} is no state`)
    }

    return state
  }
}

export function recipesOf(sent: Sent, stateOf: (block: number, what: string) => number | null): Recipes {
  return {
    recipes: sent.recipes.map((r) => ({
      ...r,
      output: r.output.map(({ item, metadata, count, block }) => ({ item, metadata, count, blockState: stateOf(block, `recipe ${r.id}`) })),
    })),
  }
}

export function entitiesOf(sent: Sent, packs: EntityDefinitions): Entities {
  return { entities: sent.entities, ...packs }
}

export function commandsOf(sent: Sent, help: Record<string, string[]>): CommandsFile {
  return { commands: sent.commands.commands.map((c) => ({ ...c, help: help[c.name] ?? [] })), enums: sent.commands.enums }
}

/** Every biome of the registry, by name, with what the server sends of it. */
export function biomesOf(sent: Sent, ids: BiomeId[]): Biomes {
  const sentBy = new Map(sent.biomes.map((b) => [b.name, b]))
  const unknown = sent.biomes.find((b) => !ids.some((id) => id.name === b.name))

  if (unknown) {
    throw new Error(`the server sends biome ${unknown.name}, which its registry does not have`)
  }

  return {
    biomes: [...ids]
      .sort((a, b) => a.name.localeCompare(b.name))
      .map((id) => {
        const definition = sentBy.get(id.name)

        if (!definition) {
          return id
        }

        const { name: _, ...rest } = definition
        return { ...id, definition: rest }
      }),
  }
}
