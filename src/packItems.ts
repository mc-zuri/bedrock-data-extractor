// The items the vanilla_<version> behavior packs define that the game does not register itself: one a file defines in
// the item format (from 1.16.100; an older file only adds components to an item the game has) and the base pack
// defines none of. Each is the newest such pack's, with its file there: the pack the server numbers it in.
import { basename } from 'node:path'
import { packFiles, packJson, vanillaPacks } from './loot.ts'
import { compareVersions } from './versions.ts'

export interface PackItem {
  name: string
  pack: string
  file: string
}

const ITEM_FORMAT = '1.16.100'

interface ItemDefinition {
  format_version?: unknown
  'minecraft:item'?: { description?: { identifier?: unknown } }
}

export function readPackItems(serverDir: string): PackItem[] {
  const newest = new Map<string, PackItem>()
  const inBase = new Set<string>()

  for (const dir of vanillaPacks(serverDir)) {
    const pack = basename(dir)

    for (const [path, text] of packFiles(dir, 'items')) {
      const definition = packJson(text, `${dir}: ${path}`) as ItemDefinition
      const name = definition['minecraft:item']?.description?.identifier
      const format = definition.format_version

      if (typeof name !== 'string') {
        continue
      }

      if (pack === 'vanilla') {
        inBase.add(name)
      } else if (typeof format === 'string' && compareVersions(format, ITEM_FORMAT) >= 0) {
        newest.set(name, { name, pack, file: basename(path) })
      }
    }
  }

  return [...newest.values()].filter((i) => !inBase.has(i.name))
}
