import assert from 'node:assert/strict'
import { test } from 'node:test'
import { f32, parsePalette, parseShapes, type BlockShapes } from '../src/blocks.ts'
import { checkPalette, checkShapes } from '../src/checks.ts'
import { paletteNbt, sampleBlocks, shapesNbt } from './helpers.ts'

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
  refused((s) => s.blocks[2].states.pop(), /499 states of minecraft:wool, the palette 500/)
  refused((s) => (s.blocks[1].name = 'minecraft:dirt'), /minecraft:dirt where the palette has minecraft:stone/)
  refused((s) => (s.blocks[2].states[0].outline = [1, 0, 0, 0, 1, 1]), /is not a box/)
  refused((s) => (s.blocks[0].states[0].collision = [[0, 0, 0, 1, 1, 1]]), /air collides/)
  refused((s) => (s.blocks[1].states[0].visual = [0, 0, 0, 1, 0.5, 1]), /stone is drawn in/)
  refused((s) => (s.blocks[1].states[0].camera = null), /stone stops the camera with/)
  refused((s) => delete s.blocks[2].states[3].camera, /some states have a camera shape and some not/)
})
