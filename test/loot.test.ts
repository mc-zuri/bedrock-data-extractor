import assert from 'node:assert/strict'
import { mkdirSync, writeFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { test } from 'node:test'
import { readBlockLoot } from '../src/loot.ts'
import { readPackItems } from '../src/packItems.ts'
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

void test('the items the versioned packs define in the item format and the base pack does not, each by the newest pack defining it', () => {
  const server = tempDir()

  function item(file: string, format: string, identifier: string): void {
    write(server, file, `{ "format_version": "${format}", "minecraft:item": { "description": { "identifier": "${identifier}" }, "components": {} } }`)
  }

  item('behavior_packs/vanilla/items/apple.json', '1.10', 'minecraft:apple')
  // an override of an item the base pack has, and an older format's components of an item the game has: not the pack's
  item('behavior_packs/vanilla_1.20.50/items/apple.json', '1.20.50', 'minecraft:apple')
  item('behavior_packs/vanilla_1.20.0/items/pitcher_pod.json', '1.10', 'minecraft:pitcher_pod')
  item('behavior_packs/vanilla_1.21.0/items/breeze_rod.json', '1.10', 'minecraft:breeze_rod')
  item('behavior_packs/vanilla_1.21.0/items/trial_key.json', '1.20.50', 'minecraft:trial_key')
  item('behavior_packs/vanilla_1.21.10/items/breeze_rod.json', '1.20.50', 'minecraft:breeze_rod')
  item('behavior_packs/vanilla_1.21.130/items/stone_spear.json', '1.21.100', 'minecraft:stone_spear')
  item('behavior_packs/vanilla_1.26.0/items/stone_spear.json', '1.21.100', 'minecraft:stone_spear')
  // not a vanilla pack: not read
  item('behavior_packs/experimental_x/items/wind_charge.json', '1.20.50', 'minecraft:wind_charge')
  assert.deepEqual(
    readPackItems(server).sort((a, b) => a.name.localeCompare(b.name)),
    [
      { name: 'minecraft:breeze_rod', pack: 'vanilla_1.21.10', file: 'breeze_rod.json' },
      { name: 'minecraft:stone_spear', pack: 'vanilla_1.26.0', file: 'stone_spear.json' },
      { name: 'minecraft:trial_key', pack: 'vanilla_1.21.0', file: 'trial_key.json' },
    ],
  )
})
