import assert from 'node:assert/strict'
import { mkdirSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { test } from 'node:test'
import { readBlockLoot } from '../src/loot.ts'
import { tempDir } from './helpers.ts'

function write(root: string, file: string, text: string): void {
  mkdirSync(dirname(join(root, file)), { recursive: true })
  writeFileSync(join(root, file), text)
}

void test('the loot tables the vanilla packs name for their blocks, a later pack over an earlier, comments and trailing commas read', () => {
  const server = tempDir()
  write(
    server,
    'behavior_packs/vanilla/blocks/slab.json',
    '{ "minecraft:block": { "description": { "identifier": "minecraft:white_wool_slab" }, "components": { "minecraft:loot": "loot_tables/blocks/slab.json" } } }',
  )
  write(server, 'behavior_packs/vanilla/loot_tables/blocks/slab.json', '{ "pools": [{ "entries": [{ "type": "item", "name": "minecraft:stone" }] }] }')
  // a later pack's definition and table over the earlier one's, read as the game reads them
  write(
    server,
    'behavior_packs/vanilla_1.21.50/blocks/slab.json',
    '{ "minecraft:block": { "description": { "identifier": "minecraft:white_wool_slab" }, "components": { "minecraft:loot": "loot_tables/blocks/slab.json" } } }',
  )
  write(server, 'behavior_packs/vanilla_1.21.50/loot_tables/blocks/slab.json', '// the slab\n{ "pools": [{ "entries": [{ "type": "item", "name": "minecraft:dirt", }] }] }')
  write(server, 'behavior_packs/vanilla_1.21.50/blocks/plain.json', '{ "minecraft:block": { "description": { "identifier": "minecraft:plain" }, "components": {} } }')
  // not a vanilla pack: not read
  write(
    server,
    'behavior_packs/experimental_x/blocks/other.json',
    '{ "minecraft:block": { "description": { "identifier": "minecraft:other" }, "components": { "minecraft:loot": "loot_tables/none.json" } } }',
  )
  assert.deepEqual(readBlockLoot(server), {
    blocks: { 'minecraft:white_wool_slab': ['loot_tables/blocks/slab.json'] },
    tables: { 'loot_tables/blocks/slab.json': { pools: [{ entries: [{ type: 'item', name: 'minecraft:dirt' }] }] } },
  })
})

void test('a block whose loot table its pack lacks is refused', () => {
  const server = tempDir()
  write(
    server,
    'behavior_packs/vanilla/blocks/slab.json',
    '{ "minecraft:block": { "description": { "identifier": "minecraft:slab" }, "components": { "minecraft:loot": "loot_tables/gone.json" } } }',
  )
  assert.throws(() => readBlockLoot(server), /minecraft:slab's loot table loot_tables\/gone\.json is not in its pack/)
})
