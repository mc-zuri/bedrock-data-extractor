// The loot tables the vanilla behavior packs' block definitions name: the drops of the data-driven blocks, which the
// game has no drop code for. The packs are behavior_packs/vanilla, then vanilla_<version>, each over the ones before it.
// A pack keeps a category's files loose (<pack>/<category>/...) or, from about 1.21, also in an archive
// (__brarchive/<category>.brarchive).
import { existsSync, readdirSync, readFileSync, statSync } from 'node:fs'
import { join, sep } from 'node:path'
import { compareVersions } from './versions.ts'

export interface BlockLoot {
  /** each block's tables (its components', then its permutations'), by path */
  blocks: Record<string, string[]>
  tables: Record<string, unknown>
}

interface BlockDefinition {
  'minecraft:block'?: {
    description?: { identifier?: string }
    components?: { 'minecraft:loot'?: string }
    permutations?: { components?: { 'minecraft:loot'?: string } }[]
  }
}

/** The vanilla behavior packs, oldest first. Experiments, editor and education packs are not on by default. */
export function vanillaPacks(serverDir: string): string[] {
  const packs = join(serverDir, 'behavior_packs')

  function version(name: string): string {
    return name === 'vanilla' ? '0' : name.slice('vanilla_'.length)
  }

  if (!existsSync(packs)) {
    return []
  }

  return readdirSync(packs)
    .filter((name) => name === 'vanilla' || /^vanilla_\d+(\.\d+)*$/.test(name))
    .sort((a, b) => compareVersions(version(a), version(b)))
    .map((name) => join(packs, name))
}

/** A pack archive's files: u64 magic, u32 count, u32 version, then 256-byte entries (name, offset, size), then the data. */
function archived(pack: string, category: string): Map<string, Buffer> {
  const file = join(pack, '__brarchive', `${category}.brarchive`)
  const out = new Map<string, Buffer>()

  if (!existsSync(file)) {
    return out
  }

  const archive = readFileSync(file)
  const count = archive.readUInt32LE(8)
  const data = 16 + count * 256

  for (let i = 0; i < count; i++) {
    const entry = 16 + i * 256
    const name = archive.toString('utf8', entry + 1, entry + 1 + archive[entry])
    const offset = archive.readUInt32LE(entry + 248)
    out.set(name, archive.subarray(data + offset, data + offset + archive.readUInt32LE(entry + 252)))
  }

  return out
}

/** A category's JSON files in a pack, archived or loose, by their path from the pack ("loot_tables/blocks/x.json"). */
export function packFiles(pack: string, category: string): Map<string, Buffer> {
  const out = new Map<string, Buffer>()

  for (const [name, text] of archived(pack, category)) {
    out.set(`${category}/${name}`, text)
  }

  const dir = join(pack, category)

  if (existsSync(dir)) {
    // by name: not in the order the machine lists them
    for (const name of (readdirSync(dir, { recursive: true }) as string[]).sort()) {
      if (name.endsWith('.json') && statSync(join(dir, name)).isFile()) {
        out.set(`${category}/${name.split(sep).join('/')}`, readFileSync(join(dir, name)))
      }
    }
  }

  return out
}

/** Comments and trailing commas left out: the game reads its packs' JSON with them, JSON.parse does not. */
function strictJson(text: string): string {
  let out = ''

  for (let i = 0; i < text.length; i++) {
    const c = text[i]

    if (c === '"') {
      let end = i + 1

      while (end < text.length && text[end] !== '"') {
        end += text[end] === '\\' ? 2 : 1
      }

      out += text.slice(i, end + 1)
      i = end
    } else if (c === '/' && text[i + 1] === '/') {
      while (i < text.length && text[i] !== '\n') {
        i++
      }

      out += '\n'
    } else if (c === '/' && text[i + 1] === '*') {
      i = text.indexOf('*/', i + 2) + 1

      if (i === 0) {
        break
      }
    } else {
      out += c
    }
  }

  return out.replace(/,(\s*[}\]])/g, '$1')
}

/** A pack file's JSON, as the game reads it. */
export function packJson(text: Buffer, where: string): unknown {
  const json = text.toString('utf8').replace(/^﻿/, '')

  try {
    return JSON.parse(json) as unknown
  } catch {
    try {
      return JSON.parse(strictJson(json)) as unknown
    } catch (e) {
      throw new Error(`${where}: ${e instanceof Error ? e.message : String(e)}`, { cause: e })
    }
  }
}

/** The tables the server's vanilla behavior packs' block definitions name, a later pack's over an earlier's. */
export function readBlockLoot(serverDir: string): BlockLoot {
  const loot: BlockLoot = { blocks: {}, tables: {} }

  for (const pack of vanillaPacks(serverDir)) {
    const tables = packFiles(pack, 'loot_tables')

    for (const [file, text] of packFiles(pack, 'blocks')) {
      const block = (packJson(text, `${pack}: ${file}`) as BlockDefinition)['minecraft:block']
      const name = block?.description?.identifier

      if (!name) {
        throw new Error(`${pack}: ${file} defines no block`)
      }

      const paths = [block.components?.['minecraft:loot'], ...(block.permutations ?? []).map((p) => p.components?.['minecraft:loot'])].filter((p) => p !== undefined)

      if (!paths.length) {
        continue
      }

      loot.blocks[name] = [...new Set(paths)]

      for (const path of paths) {
        const table = tables.get(path)

        if (table) {
          loot.tables[path] = packJson(table, `${pack}: ${path}`)
        } else if (!(path in loot.tables)) {
          throw new Error(`${pack}: ${name}'s loot table ${path} is not in its pack`)
        }
      }
    }
  }

  return loot
}
