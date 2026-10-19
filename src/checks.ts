import type { BlockFacts, BlockShapes, BlockState, BlockTypes, Box, Enchantments, Items, StateFacts, StateShapes } from './blocks.ts'
import type { Language } from './language.ts'
import type { Effect, Enums, Tints } from './registries.ts'
import type { Attributes, Biomes, CommandsFile, Entities, Recipes } from './sent.ts'
import type { Skin } from './steve.ts'

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

const MIN_ITEMS = 400

export function checkItems({ items, aliases, complexAliases }: Items): void {
  if (items.length < MIN_ITEMS) {
    throw new Error(`the item registry has only ${items.length} items`)
  }

  const ids = new Set(items.map((i) => i.id))

  if (ids.size !== items.length) {
    throw new Error('two items have one id')
  }

  const byName = new Map(items.map((i) => [i.name, i]))

  for (const [name, key, expected] of [
    ['minecraft:stone', 'maxStackSize', 64],
    ['minecraft:diamond_sword', 'maxStackSize', 1],
    ['minecraft:egg', 'maxStackSize', 16],
    ['minecraft:bucket', 'maxStackSize', 16],
    ['minecraft:water_bucket', 'maxStackSize', 1],
    ['minecraft:diamond_sword', 'maxDamage', 1561],
    ['minecraft:stone', 'maxDamage', 0],
    ['minecraft:diamond_sword', 'enchantSlots', 16],
    ['minecraft:bow', 'enchantSlots', 32],
  ] as const) {
    const value = byName.get(name)?.[key]

    if (value !== expected) {
      throw new Error(`${name}'s ${key} is ${value}, not ${expected}`)
    }
  }

  if (!byName.get('minecraft:diamond_sword')?.repairItems.includes('minecraft:diamond')) {
    throw new Error('the diamond sword is not repaired with diamonds')
  }

  if (!byName.get('minecraft:diamond_sword')?.tags.includes('minecraft:is_sword')) {
    throw new Error('the diamond sword is not tagged minecraft:is_sword')
  }

  const unknown = [...items.flatMap((i) => i.repairItems), ...aliases.map((a) => a.item), ...complexAliases.flatMap((a) => a.items)].find((name) => !byName.has(name))

  if (unknown) {
    throw new Error(`${unknown} is no registered item`)
  }
}

function sameRuns(blocks: { name: string; states: unknown[] }[], states: BlockState[], what: string): void {
  let next = 0

  for (const { name, states: of } of blocks) {
    if (states[next]?.name !== name) {
      throw new Error(`the ${what} have ${name} where the palette has ${states[next]?.name ?? 'no more states'}`)
    }

    const run = states.slice(next).findIndex((s) => s.name !== name)

    if (of.length !== (run < 0 ? states.length - next : run)) {
      throw new Error(`the ${what} have ${of.length} states of ${name}, the palette ${run < 0 ? states.length - next : run}`)
    }

    next += of.length
  }

  if (next !== states.length) {
    throw new Error(`the ${what} cover ${next} of the palette's ${states.length} states`)
  }
}

export function checkBlocks({ blocks, lootTables }: BlockTypes, states: BlockState[], { items }: Items): void {
  sameRuns(blocks, states, 'block facts')
  const byName = new Map(blocks.map((b) => [b.name, b]))
  const itemNames = new Set(items.map((i) => i.name))

  if (new Set(blocks.map((b) => b.id)).size !== blocks.length) {
    throw new Error('two blocks have one id')
  }

  for (const b of blocks) {
    if (b.defaultState < 0 || b.defaultState >= b.states.length) {
      throw new Error(`${b.name}: its default state ${b.defaultState} is none of its ${b.states.length}`)
    }

    for (const path of b.loot ?? []) {
      if (!(path in lootTables)) {
        throw new Error(`${b.name}: its loot table ${path} is not there`)
      }
    }

    b.states.forEach((s, i) => {
      const named = [...Object.keys(s.destroySpeeds), ...s.harvestedBy, ...s.drops.items.map((d) => d.item)]
      const unknown = named.find((item) => !itemNames.has(item))

      if (unknown) {
        throw new Error(`${b.name} #${i}: ${unknown} is no registered item`)
      }

      if (s.lightEmission < 0 || s.lightEmission > 15 || s.lightDampening < 0 || s.lightDampening > 15) {
        throw new Error(`${b.name} #${i}: light ${s.lightEmission} / ${s.lightDampening}`)
      }

      for (const d of s.drops.items) {
        if (d.hits < 1 || d.hits > s.drops.samples || d.minCount < 1 || d.maxCount < d.minCount) {
          throw new Error(`${b.name} #${i}: ${d.item} drops ${JSON.stringify(d)}`)
        }
      }
    })
  }

  function block(name: string, oldName?: string): BlockFacts {
    const found = byName.get(name) ?? (oldName === undefined ? undefined : byName.get(oldName))

    if (!found) {
      throw new Error(`no ${name}`)
    }

    return found
  }

  function def(name: string, oldName?: string): StateFacts {
    const b = block(name, oldName)
    return b.states[b.defaultState]
  }

  function dropped(name: string): string {
    return def(name)
      .drops.items.map((d) => d.item)
      .join(', ')
  }

  const known: [string, unknown, unknown][] = [
    ['air id', block('minecraft:air').id, 0],
    ['stone id', block('minecraft:stone').id, 1],
    ['stone hardness', def('minecraft:stone').hardness, 1.5],
    ['stone explosion resistance', def('minecraft:stone').explosionResistance, 6],
    ['stone light dampening', def('minecraft:stone').lightDampening, 15],
    ['bedrock hardness', def('minecraft:bedrock').hardness, -1],
    ['glowstone light emission', def('minecraft:glowstone').lightEmission, 15],
    ['glass light dampening', def('minecraft:glass').lightDampening, 0],
    ['ice friction', def('minecraft:ice').friction, 0.98],
    ['planks flame odds', def('minecraft:oak_planks', 'minecraft:planks').flameOdds, 5],
    ['planks burn odds', def('minecraft:oak_planks', 'minecraft:planks').burnOdds, 20],
    ['glass translucency', block('minecraft:glass').translucency, 1],
    ['oak stairs can contain liquid', def('minecraft:oak_stairs').canContainLiquid, true],
    ['stone can contain liquid', def('minecraft:stone').canContainLiquid, false],
    ['stone requires the correct tool', block('minecraft:stone').requiresCorrectTool, true],
    ['the diamond pickaxe on stone', def('minecraft:stone').destroySpeeds['minecraft:diamond_pickaxe'], 8],
    ['the wooden pickaxe harvests stone', def('minecraft:stone').harvestedBy.includes('minecraft:wooden_pickaxe'), true],
    ['stone drops', dropped('minecraft:stone'), 'minecraft:cobblestone'],
    ['dirt drops', dropped('minecraft:dirt'), 'minecraft:dirt'],
    ['glass drops', dropped('minecraft:glass'), ''],
  ]

  for (const [what, value, expected] of known) {
    if (value !== expected) {
      throw new Error(`${what}: ${String(value)}, not ${String(expected)}`)
    }
  }
}

export function checkEnchantments({ enchantments }: Enchantments): void {
  if (enchantments.length < 30) {
    throw new Error(`only ${enchantments.length} enchantments`)
  }

  enchantments.forEach((e, i) => {
    if (e.id !== i) {
      throw new Error(`enchantment ${i} has id ${e.id}`)
    }

    if (e.minCost.length !== Math.max(e.maxLevel, 2) || e.maxCost.length !== e.minCost.length || e.incompatible.some((id) => id < 0 || id >= enchantments.length)) {
      throw new Error(`${e.name}: ${e.minCost.length} costs for ${e.maxLevel} levels, or an incompatible id that is none`)
    }
  })

  const protection = enchantments[0]

  if (protection.name !== 'protection' || protection.maxLevel !== 4 || protection.primarySlots !== 0xf) {
    throw new Error(`enchantment 0 is ${protection.name} (max level ${protection.maxLevel}, slots ${protection.primarySlots}), not protection`)
  }
}

// biome ids every build has had
const KNOWN_BIOMES: Record<string, number> = { ocean: 0, plains: 1, desert: 2, extreme_hills: 3, forest: 4, taiga: 5, swampland: 6, river: 7, hell: 8, the_end: 9 }

export function checkBiomes({ biomes }: Biomes): void {
  const byName = new Map(biomes.map((b) => [b.name, b.id]))

  if (biomes.length < 60) {
    throw new Error(`only ${biomes.length} biomes`)
  }

  if (byName.size !== biomes.length || new Set(byName.values()).size !== biomes.length) {
    throw new Error('a biome name or id twice')
  }

  for (const [name, id] of Object.entries(KNOWN_BIOMES)) {
    if (byName.get(name) !== id) {
      throw new Error(`biome ${name} has id ${byName.get(name)}, not ${id}`)
    }
  }

  if (!biomes.find((b) => b.name === 'plains')?.definition) {
    throw new Error('the server sends no definition of plains')
  }
}

// the ids, names and harm of the effects Bedrock has had since before 1.16
const KNOWN_EFFECTS: [number, string, boolean][] = [
  [1, 'speed', false],
  [2, 'slowness', true],
  [7, 'instant_damage', true],
  [10, 'regeneration', false],
  [19, 'poison', true],
  [25, 'fatal_poison', true],
  [26, 'conduit_power', false],
  [29, 'village_hero', false],
]

export function checkEffects(effects: Effect[]): void {
  effects.forEach((e, i) => {
    if (e.id !== i + 1) {
      throw new Error(`effect ${i + 1} has id ${e.id}`)
    }

    if (!/^[a-z_]+$/.test(e.name) || !/^(potion|effect)\.\w+$/.test(e.descriptionId)) {
      throw new Error(`effect ${e.id}: ${e.name} / ${e.descriptionId}`)
    }
  })

  if (new Set(effects.map((e) => e.name)).size !== effects.length) {
    throw new Error('an effect name twice')
  }

  for (const [id, name, harmful] of KNOWN_EFFECTS) {
    const e = effects.at(id - 1)

    if (e?.name !== name || e.harmful !== harmful) {
      throw new Error(`effect ${id} is ${e?.name} (harmful: ${e?.harmful}), not ${name} (${harmful})`)
    }
  }
}

// what every build names so
const KNOWN_ENUMS: Partial<Record<keyof Enums, [number, string][]>> = {
  sounds: [
    [0, 'item.use.on'],
    [1, 'hit'],
    [2, 'step'],
  ],
  particles: [[1, 'bubble']],
  containers: [
    [-1, 'INVENTORY'],
    [0, 'CONTAINER'],
    [1, 'WORKBENCH'],
    [2, 'FURNACE'],
  ],
}

export function checkEnums(enums: Enums): void {
  for (const [key, list] of Object.entries(enums) as [keyof Enums, Enums[keyof Enums]][]) {
    // the exploration map registry's order, not by id
    if (key === 'explorationMarkers') {
      continue
    }

    // a sound event or particle type may have an alias: its id twice, under each name (1.21.60's particle 90)
    const aliased = key === 'sounds' || key === 'particles'

    if (list.some((e, i) => i > 0 && (e.id < list[i - 1].id || (e.id === list[i - 1].id && !aliased)))) {
      throw new Error(`${key}: not by id, or an id twice`)
    }

    // two of 1.16.201's containers share a name
    if (key !== 'containerNames' && new Set(list.map((e) => e.name)).size !== list.length) {
      throw new Error(`${key}: a name twice`)
    }

    for (const [id, name] of KNOWN_ENUMS[key] ?? []) {
      if (list.find((e) => e.id === id)?.name !== name) {
        throw new Error(`${key}: ${id} is not ${name}`)
      }
    }
  }

  if (enums.instruments.length && !/^(note\.)?harp$/.test(enums.instruments[0].name)) {
    throw new Error(`instrument 0 is ${enums.instruments[0].name}`)
  }

  if (enums.mapDecorations.length && enums.mapDecorations[0].name !== 'MarkerWhite') {
    throw new Error(`map decoration 0 is ${enums.mapDecorations[0].name}`)
  }

  // 1.16.201 names them combined_hotbar_and_inventory_items, crafting_input_items, cursor_items
  const known = ['hotbar_and_inventory', 'crafting_input', 'cursor'].every((n) => enums.containerNames.some((e) => [n, `${n}_items`, `combined_${n}_items`].includes(e.name)))

  if (!known) {
    throw new Error('container names: not the known ones')
  }
}

export function checkTints({ biomes, redstone, constant }: Tints, { biomes: registered }: Biomes, states: BlockState[]): void {
  const names = new Set(registered.map((b) => b.name))
  const unknown = biomes.find((b) => !names.has(b.name))

  if (unknown || new Set(biomes.map((b) => b.name)).size !== biomes.length || !biomes.some((b) => b.name === 'plains')) {
    throw new Error(`tints: ${unknown ? `biome ${unknown.name} is not registered` : 'a biome twice, or no plains'}`)
  }

  const wire = states.filter((s) => s.name === 'minecraft:redstone_wire').length

  if (redstone.length && (redstone.length !== wire || new Set(redstone).size < 2)) {
    throw new Error(`tints: ${redstone.length} redstone wire colours for ${wire} states, ${new Set(redstone).size} of them distinct`)
  }

  const blocks = new Set(states.map((s) => s.name))
  const unplaced = constant.find((c) => !blocks.has(c.name))

  if (unplaced) {
    throw new Error(`tints: ${unplaced.name} is no block`)
  }
}

export function checkRecipes({ recipes }: Recipes, { items }: Items): void {
  const names = new Set(items.map((i) => i.name))
  const crafting = recipes.filter((r) => r.block === 'crafting_table')

  if (crafting.length < 1000) {
    throw new Error(`only ${crafting.length} recipes at the crafting table`)
  }

  for (const r of recipes) {
    const unknown = [...r.input.flatMap((i) => (i && 'item' in i ? [i.item] : [])), ...r.output.map((o) => o.item)].find((name) => !names.has(name))

    if (unknown || !r.output.length) {
      throw new Error(`recipe ${r.id}: ${unknown ? `${unknown} is no registered item` : 'no output'}`)
    }
  }

  const table = crafting.find((r) => r.output[0].item === 'minecraft:crafting_table' && r.type === 'shaped')

  if (table?.width !== 2 || table.height !== 2) {
    throw new Error('no 2 x 2 recipe of the crafting table')
  }
}

export function checkEntities({ entities, definitions, lootTables }: Entities): void {
  const ids = new Set(entities.map((e) => e.id))

  if (entities.length < 100 || ids.size !== entities.length) {
    throw new Error(`${entities.length} entities, ${ids.size} ids`)
  }

  for (const name of ['minecraft:player', 'minecraft:zombie', 'minecraft:cow']) {
    if (!entities.some((e) => e.name === name) || !(name in definitions)) {
      throw new Error(`${name} is not sent, or not defined`)
    }
  }

  if (!('loot_tables/entities/zombie.json' in lootTables)) {
    throw new Error("the zombie's loot table is not there")
  }
}

export function checkCommands({ commands, enums }: CommandsFile): void {
  if (commands.length < 40) {
    throw new Error(`only ${commands.length} commands`)
  }

  const undescribed = commands.find((c) => !c.help.some((line) => line.startsWith('- /')))

  if (undescribed) {
    throw new Error(`the console gives no usage of ${undescribed.name}: ${JSON.stringify(undescribed.help)}`)
  }

  if (!commands.some((c) => c.name === 'teleport' && c.aliases.includes('tp')) || !enums.GameMode?.length) {
    throw new Error('no teleport (tp), or no GameMode values')
  }
}

export function checkAttributes({ attributes }: Attributes): void {
  const health = attributes.find((a) => a.name === 'minecraft:health')

  if (health?.max !== 20 || health.default !== 20 || !attributes.some((a) => a.name === 'minecraft:movement')) {
    throw new Error(`the player's health is ${JSON.stringify(health)}, or it has no movement`)
  }
}

export function checkSteve(skin: Skin): void {
  const { SkinData, SkinImageWidth, SkinImageHeight, SkinId } = skin

  if (typeof SkinData !== 'string' || typeof SkinImageWidth !== 'number' || typeof SkinImageHeight !== 'number' || typeof SkinId !== 'string') {
    throw new Error('steve: no SkinData, SkinImageWidth, SkinImageHeight or SkinId')
  }

  if (Buffer.from(SkinData, 'base64').length !== SkinImageWidth * SkinImageHeight * 4) {
    throw new Error(`steve: SkinData is not ${SkinImageWidth} x ${SkinImageHeight} RGBA`)
  }

  const texts = [SkinId, ...['SkinResourcePatch', 'SkinGeometryData'].map((k) => (typeof skin[k] === 'string' ? Buffer.from(skin[k], 'base64').toString('utf8') : ''))]
  const account = texts.flatMap((t) => t.match(/persona[-_][0-9a-z]+-\d+/gi) ?? []).find((id) => !/^persona[-_]123456789012345-0$/i.test(id))

  if (account) {
    throw new Error(`steve: a persona id of an account is left (${account})`)
  }
}
