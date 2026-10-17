import type { BlockShapes, BlockState, Box, StateShapes } from './blocks.ts'
import type { Language } from './language.ts'

const LANGUAGE_KEY = /^[a-zA-Z.:0-9_-]+$/
const MIN_VANILLA_STRINGS = 5000

export function checkLanguage(language: Language): void {
  const vanilla = Object.keys(language.packs.vanilla ?? {})

  if (vanilla.length < MIN_VANILLA_STRINGS) {
    throw new Error(`the vanilla pack has ${vanilla.length} strings, fewer than the ${MIN_VANILLA_STRINGS} every release has`)
  }

  for (const [pack, strings] of Object.entries(language.packs)) {
    const bad = Object.keys(strings).filter((key) => !LANGUAGE_KEY.test(key))

    if (bad.length) {
      throw new Error(`the ${pack} pack has keys that are not language keys: ${bad.slice(0, 5).join(', ')}`)
    }
  }
}

const PROPERTY_TYPES = new Set(['byte', 'int', 'string'])
const MIN_STATES = 400
const UNIT_CUBE = JSON.stringify([0, 0, 0, 1, 1, 1])

function sortedProperties(state: BlockState): [string, unknown][] {
  return Object.keys(state.states)
    .sort()
    .map((k) => [k, state.states[k]])
}

export function checkPalette(states: BlockState[]): void {
  if (states.length < MIN_STATES) {
    throw new Error(`the palette has only ${states.length} block states`)
  }

  const blocks = new Set<string>()
  const seen = new Set<string>()

  states.forEach((s, i) => {
    const at = `palette state ${i} (${s.name})`

    if (!s.name.startsWith('minecraft:')) {
      throw new Error(`${at}: no minecraft: name`)
    }

    for (const [k, v] of Object.entries(s.states)) {
      if (!PROPERTY_TYPES.has(v.type)) {
        throw new Error(`${at}: ${k} is a ${v.type}`)
      }
    }

    if (s.name !== states[i - 1]?.name) {
      if (blocks.has(s.name)) {
        throw new Error(`${at}: the states of ${s.name} are not together`)
      }

      blocks.add(s.name)
    }

    const key = JSON.stringify([s.name, sortedProperties(s)])

    if (seen.has(key)) {
      throw new Error(`${at}: there twice`)
    }

    seen.add(key)
  })

  if (!blocks.has('minecraft:air')) {
    throw new Error('the palette has no minecraft:air')
  }
}

function isBox(box: Box): boolean {
  if (box.length !== 6 || !box.every(Number.isFinite)) {
    return false
  }

  const [minX, minY, minZ, maxX, maxY, maxZ] = box

  return minX <= maxX && minY <= maxY && minZ <= maxZ
}

export function checkShapes(shapes: BlockShapes, states: BlockState[]): void {
  let next = 0

  for (const { name, states: shaped } of shapes.blocks) {
    const count = states.slice(next).findIndex((s) => s.name !== name)
    const run = count < 0 ? states.length - next : count

    if (!run) {
      throw new Error(`the shapes have ${name} where the palette has ${states[next]?.name ?? 'no more states'}`)
    }

    if (shaped.length !== run) {
      throw new Error(`the shapes have ${shaped.length} states of ${name}, the palette ${run}`)
    }

    shaped.forEach(({ collision, visual, ui, outline, camera }, i) => {
      const bad = [...collision, visual, ui, outline, ...(camera ? [camera] : [])].find((box) => !isBox(box))

      if (bad) {
        throw new Error(`${name} #${i}: [${bad.join(', ')}] is not a box`)
      }
    })

    if (shaped.some((s) => (s.camera === undefined) !== (shapes.blocks[0].states[0].camera === undefined))) {
      throw new Error(`${name}: some states have a camera shape and some not`)
    }

    next += run
  }

  if (next !== states.length) {
    throw new Error(`the shapes cover ${next} of the palette's ${states.length} states`)
  }

  function shapesOf(name: string): StateShapes[] | undefined {
    return shapes.blocks.find((b) => b.name === name)?.states
  }

  if (shapesOf('minecraft:air')?.some((s) => s.collision.length)) {
    throw new Error('air collides')
  }

  const stone = shapesOf('minecraft:stone')?.[0]

  if (!stone) {
    throw new Error('no minecraft:stone')
  }

  for (const [what, shape] of [
    ['collides with', stone.collision.length === 1 ? stone.collision[0] : stone.collision.flat()],
    ['is drawn in', stone.visual],
    ['is outlined in the UI by', stone.ui],
    ['is outlined by', stone.outline],
    ...(stone.camera === undefined ? [] : [['stops the camera with', stone.camera ?? []] as const]),
  ] as const) {
    if (JSON.stringify(shape) !== UNIT_CUBE) {
      throw new Error(`stone ${what} [${shape.join(', ')}], not the unit cube`)
    }
  }
}
