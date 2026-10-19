import assert from 'node:assert/strict'
import { test } from 'node:test'
import {
  factsAsOnWindows,
  itemsAsOnWindows,
  parseBlockTypes,
  parseItems,
  parsePalette,
  parseShapes,
  shapesAsOnWindows,
  type BlockShapes,
  type BlockTypes,
  type Items,
} from '../src/blocks.ts'
import { f32 } from '../src/util.ts'
import { checkBlocks, checkItems, checkPalette, checkShapes } from '../src/checks.ts'
import { blocksNbt, itemsNbt, paletteNbt, sampleBlocks, sampleItems, shapesNbt } from './helpers.ts'

function last<T>(list: T[]): T {
  const entry = list.at(-1)
  assert.ok(entry)
  return entry
}

void test('a float32 is written as the shortest decimal that reads back as it', () => {
  assert.equal(f32(Math.fround(0.95)), 0.95)
  assert.equal(f32(Math.fround(0.95000005)), 0.95000005)
  assert.equal(f32(Math.fround(0.1825)), 0.1825)
  assert.equal(f32(1), 1)
})

void test("the agent's palette and shapes read back as they were written", () => {
  const { states, shapes } = sampleBlocks()
  assert.deepEqual(parsePalette(paletteNbt(states)), states)
  assert.deepEqual(parseShapes(shapesNbt(shapes)), shapes)
  // a build before 1.20.30: no camera
  const old: BlockShapes = {
    blocks: shapes.blocks.map((b) => ({ name: b.name, states: b.states.map(({ collision, visual, ui, outline }) => ({ collision, visual, ui, outline })) })),
  }
  assert.deepEqual(parseShapes(shapesNbt(old)), old)
})

void test('a palette is refused with too few states, a state twice, a block split, an odd property, or no air', () => {
  const { states } = sampleBlocks()
  checkPalette(states)
  assert.throws(() => checkPalette(states.slice(0, 100)), /only 100 block states/)
  assert.throws(() => checkPalette([...states, states[5]]), /there twice|not together/)
  assert.throws(() => checkPalette([...states, { ...states[0], name: 'minecraft:stone' }]), /not together/)
  assert.throws(() => checkPalette([...states, { name: 'minecraft:tnt', states: { x: { type: 'float', value: 1 } }, version: 1 }]), /x is a float/)
  assert.throws(() => checkPalette(states.slice(1)), /no minecraft:air/)
})

void test('shapes are refused where they do not follow the palette, a box is not one, air collides, or stone is not a cube', () => {
  const { states, shapes } = sampleBlocks()
  checkShapes(shapes, states)
  // the sample with one change made, refused with `why`
  function refused(change: (s: BlockShapes) => unknown, why: RegExp): void {
    const changed = structuredClone(shapes)
    change(changed)
    assert.throws(() => checkShapes(changed, states), why)
  }
  refused((s) => last(s.blocks).states.pop(), /499 states of minecraft:wool, the palette 500/)
  refused((s) => (s.blocks[1].name = 'minecraft:dirt'), /minecraft:dirt where the palette has minecraft:stone/)
  refused((s) => (last(s.blocks).states[0].outline = [1, 0, 0, 0, 1, 1]), /is not a box/)
  refused((s) => (s.blocks[0].states[0].collision = [[0, 0, 0, 1, 1, 1]]), /air collides/)
  refused((s) => (s.blocks[1].states[0].visual = [0, 0, 0, 1, 0.5, 1]), /stone is drawn in/)
  refused((s) => (s.blocks[1].states[0].camera = null), /stone stops the camera with/)
  refused((s) => delete last(s.blocks).states[3].camera, /some states have a camera shape and some not/)
})

void test("the agent's block facts and items read back as they were written", () => {
  const { blocks, items } = sampleBlocks()
  assert.deepEqual(
    parseBlockTypes(blocksNbt(blocks.blocks)),
    // the loot tables are read from the zip, not the agent
    blocks.blocks.map((b) => {
      const read = { ...b }
      delete read.loot
      return read
    }),
  )
  // what the server sends is not the agent's
  assert.deepEqual(parseItems(itemsNbt(items)), { ...items, creative: [] })
})

void test('items are refused with too few, an id twice, a known answer otherwise, or a name no item has', () => {
  const { items } = sampleBlocks()
  checkItems(items)
  function refused(change: (i: Items) => unknown, why: RegExp): void {
    const changed = structuredClone(items)
    change(changed)
    assert.throws(() => checkItems(changed), why)
  }
  refused((i) => i.items.splice(20), /only 20 items/)
  refused((i) => (i.items[1].id = 0), /two items have one id/)
  refused((i) => {
    const bucket = i.items.find((x) => x.name === 'minecraft:bucket')
    assert.ok(bucket)
    bucket.maxStackSize = 64
  }, /minecraft:bucket's maxStackSize is 64, not 16/)
  refused((i) => (i.aliases[0].item = 'minecraft:nothing'), /minecraft:nothing is no registered item/)
})

void test('block facts are refused where they do not follow the palette, a known fact is otherwise, or an item is no registered one', () => {
  const { blocks, states, items } = sampleBlocks()
  checkBlocks(blocks, states, items)
  function refused(change: (b: BlockTypes) => unknown, why: RegExp): void {
    const changed = structuredClone(blocks)
    change(changed)
    assert.throws(() => checkBlocks(changed, states, items), why)
  }
  refused((b) => last(b.blocks).states.pop(), /499 states of minecraft:wool, the palette 500/)
  refused((b) => (b.blocks[1].id = 0), /two blocks have one id/)
  refused((b) => (b.blocks[6].states[0].friction = 0.6), /ice friction: 0.6, not 0.98/)
  refused((b) => (b.blocks[1].states[0].drops.items[0].item = 'minecraft:gravel'), /minecraft:gravel is no registered item/)
  refused((b) => (last(b.blocks).loot = ['loot_tables/blocks/none.json']), /its loot table loot_tables\/blocks\/none\.json is not there/)
  refused((b) => (b.blocks[1].defaultState = 3), /its default state 3 is none of its 1/)
})

void test("a Linux server's shapes are the Windows server's: before 1.26.20 the chests' top is 0.95, as MSVC folds it", () => {
  function chest(top: number, camera?: number[] | null): BlockShapes {
    const box = [0.025, 0, 0.025, 0.975, top, 0.975]
    return { blocks: [{ name: 'minecraft:chest', states: [{ collision: [box], visual: box, ui: box, outline: box, ...(camera === undefined ? {} : { camera }) }] }] }
  }

  const linux = chest(0.95000005, [0.025, 0, 0.025, 0.975, 0.95000005, 0.975])
  assert.deepEqual(shapesAsOnWindows(linux, '1.26.10.4', 'linux'), chest(0.95, [0.025, 0, 0.025, 0.975, 0.95, 0.975]))
  assert.deepEqual(shapesAsOnWindows(chest(0.95000005, null), '1.16.201.02', 'linux'), chest(0.95, null))
  assert.deepEqual(shapesAsOnWindows(chest(0.95000005), '1.16.201.02', 'linux'), chest(0.95))
  assert.equal(shapesAsOnWindows(linux, '1.26.20.5', 'linux'), linux, 'from 1.26.20 both are built with clang')
  assert.equal(shapesAsOnWindows(linux, '1.26.10.4', 'windows'), linux)
})

void test("a Linux server's explosion resistances from 1.21.42 to 1.26.10 are the Windows server's: the quotient by 5 as MSVC rounds it", () => {
  function resisting(values: number[]): BlockTypes {
    const { blocks } = sampleBlocks()
    return { ...blocks, blocks: [{ ...blocks.blocks[0], states: values.map((explosionResistance) => ({ ...blocks.blocks[0].states[0], explosionResistance })) }] }
  }

  const linux = resisting([4.2, 1.8, 0.65, 6, 3600000])
  assert.deepEqual(factsAsOnWindows(linux, '1.21.42.01', 'linux'), resisting([4.2000003, 1.8000001, 0.65000004, 6, 3600000]))
  assert.deepEqual(factsAsOnWindows(linux, '1.26.10.4', 'linux'), resisting([4.2000003, 1.8000001, 0.65000004, 6, 3600000]))
  assert.equal(factsAsOnWindows(linux, '1.21.30.03', 'linux'), linux, "before, the agent divides the type's by 5 on each platform")
  assert.equal(factsAsOnWindows(linux, '1.26.20.5', 'linux'), linux, 'from 1.26.20 both are built with clang')
  assert.equal(factsAsOnWindows(linux, '1.21.42.01', 'windows'), linux)
})

void test("a Linux server's ids of the items a pack defines are the Windows server's: each pack's ids in its files' NTFS order", () => {
  function numbered(ids: Record<string, number>): Items {
    const items = sampleItems()
    const first = items.items[0]
    return { ...items, items: Object.entries(ids).map(([name, id]) => ({ ...first, name: `minecraft:${name}`, id })) }
  }

  const packItems = [
    { name: 'minecraft:wind_charge', pack: 'vanilla_1.21.0', file: 'wind_charge.json' },
    { name: 'minecraft:trial_key', pack: 'vanilla_1.21.0', file: 'trial_key.json' },
    { name: 'minecraft:ominous_trial_key', pack: 'vanilla_1.21.0', file: 'ominous_trial_key.json' },
    { name: 'minecraft:breeze_rod', pack: 'vanilla_1.21.10', file: 'breeze_rod.json' },
    // '_' after the letters: light_blue before lime, as NTFS lists them
    { name: 'minecraft:lime_bundle', pack: 'vanilla_1.21.40', file: 'lime_bundle.json' },
    { name: 'minecraft:light_blue_bundle', pack: 'vanilla_1.21.40', file: 'light_blue_bundle.json' },
    { name: 'minecraft:light_bundle', pack: 'vanilla_1.21.40', file: 'Light_Bundle.json' },
  ]
  const linux = numbered({ apple: 257, wind_charge: 258, trial_key: 259, ominous_trial_key: 260, breeze_rod: 261, lime_bundle: 262, light_blue_bundle: 263, light_bundle: 264 })
  assert.deepEqual(
    itemsAsOnWindows(linux, packItems, '1.21.20.03', 'linux'),
    numbered({ apple: 257, ominous_trial_key: 258, trial_key: 259, wind_charge: 260, breeze_rod: 261, light_blue_bundle: 262, light_bundle: 263, lime_bundle: 264 }),
  )
  assert.equal(itemsAsOnWindows(linux, packItems, '1.26.40.8', 'linux'), linux, 'from 1.26.40 the Linux server numbers them in name order too')
  assert.equal(itemsAsOnWindows(linux, packItems, '1.21.20.03', 'windows'), linux)
})
