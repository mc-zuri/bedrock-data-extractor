// The entities the vanilla behavior packs define (each minecraft:entity's description, components, component groups and
// events), a later pack's over an earlier's, and every loot table their minecraft:loot reaches. A definition without an
// identifier (vanilla_1.16.100's npc.json) adds its components to the one an earlier pack has at its path. A table a
// definition names that no pack has (the piglin's: its drops are code) is left out; an entry of type loot_table names
// another table, kept too.
import { packFiles, packJson, vanillaPacks } from './loot.ts'

export interface EntityDefinition {
  description?: { identifier?: string }
  components?: Record<string, unknown>
  component_groups?: Record<string, Record<string, unknown>>
  events?: Record<string, unknown>
}

export interface EntityDefinitions {
  /** by identifier */
  definitions: Record<string, EntityDefinition>
  /** by path */
  lootTables: Record<string, unknown>
}

interface Pool {
  entries?: { type?: string; name?: string; pools?: Pool[] }[]
}

/** The minecraft:loot tables of an entity's components and component groups */
function lootTablesOf(definition: EntityDefinition): string[] {
  return [definition.components, ...Object.values(definition.component_groups ?? {})].flatMap((components) => {
    const table = (components?.['minecraft:loot'] as { table?: unknown } | undefined)?.table
    return typeof table === 'string' ? [table] : []
  })
}

export function readEntities(serverDir: string): EntityDefinitions {
  const byPath = new Map<string, EntityDefinition>()
  const tables = new Map<string, Buffer>()

  for (const pack of vanillaPacks(serverDir)) {
    for (const [path, text] of packFiles(pack, 'loot_tables')) {
      tables.set(path, text)
    }

    for (const [file, text] of packFiles(pack, 'entities')) {
      const entity = (packJson(text, `${pack}: ${file}`) as { 'minecraft:entity'?: EntityDefinition })['minecraft:entity']
      const earlier = byPath.get(file)

      if (entity?.description?.identifier) {
        const { description, components, component_groups: groups, events } = entity
        byPath.set(file, { description, ...(components ? { components } : {}), ...(groups ? { component_groups: groups } : {}), ...(events ? { events } : {}) })
      } else if (entity && earlier) {
        byPath.set(file, {
          ...earlier,
          components: { ...earlier.components, ...entity.components },
          component_groups: { ...earlier.component_groups, ...entity.component_groups },
        })
      } else {
        throw new Error(`${pack}: ${file} defines no entity`)
      }
    }
  }

  const out: EntityDefinitions = { definitions: {}, lootTables: {} }

  function keep(path: string): void {
    const text = tables.get(path)

    if (path in out.lootTables || !text) {
      return
    }

    const table = packJson(text, path) as { pools?: Pool[] }
    out.lootTables[path] = table

    function nested(pools: Pool[] | undefined): void {
      for (const pool of pools ?? []) {
        for (const entry of pool.entries ?? []) {
          if (entry.type === 'loot_table' && entry.name) {
            keep(entry.name)
          }

          nested(entry.pools)
        }
      }
    }

    nested(table.pools)
  }

  const definitions = [...byPath.values()].map((d): [string, EntityDefinition] => [d.description?.identifier ?? '', d]).sort(([a], [b]) => a.localeCompare(b))

  for (const [name, definition] of definitions) {
    out.definitions[name] = definition

    for (const table of lootTablesOf(definition)) {
      keep(table)
    }
  }

  return out
}
