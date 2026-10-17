import nbt from 'prismarine-nbt'
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { runAgent } from './agent.ts'
import { checkPalette, checkShapes } from './checks.ts'
import { agentHost } from './platform.ts'
import { unpackServer } from './server.ts'
import type { Platform } from './versions.ts'

export const PALETTE_FILE = 'block_palette.nbt'
export const SHAPES_FILE = 'block_shapes.json.gz'

export type Box = number[]

export interface StateShapes {
  collision: Box[]
  visual: Box
  ui: Box
  outline: Box
  camera?: Box | null
}

export interface BlockShapes {
  blocks: { name: string; states: StateShapes[] }[]
}

export interface BlockState {
  name: string
  states: Record<string, { type: string; value: unknown }>
  version: number
}

export interface Blocks {
  palette: Buffer
  states: BlockState[]
  shapes: BlockShapes
}

/** The shortest decimal that reads back as the same float32 (what the game's floats are). */
export function f32(value: number): number {
  if (!Number.isFinite(value) || Math.fround(value) !== value) {
    return value
  }

  for (let precision = 1; precision < 17; precision++) {
    const candidate = Number(value.toPrecision(precision))

    if (Math.fround(candidate) === value) {
      return candidate
    }
  }

  return value
}

interface Tag<T = unknown> {
  type: string
  value: T
}

export function parsePalette(raw: Buffer): BlockState[] {
  const root = nbt.parseUncompressed(raw, 'big') as Tag<{ blocks?: Tag<Tag<Partial<Record<'name' | 'states' | 'version', Tag>>[]>> }>
  const blocks = root.value.blocks

  if (blocks?.type !== 'list' || blocks.value.type !== 'compound') {
    throw new Error(`${PALETTE_FILE}: no blocks list of compounds`)
  }

  return blocks.value.value.map(({ name, states, version }, i) => {
    if (name?.type !== 'string' || states?.type !== 'compound' || version?.type !== 'int') {
      throw new Error(`${PALETTE_FILE}: state ${i} is not { name, states, version }`)
    }

    return {
      name: name.value as string,
      states: states.value as BlockState['states'],
      version: version.value as number,
    }
  })
}

function box(value: unknown, what: string): Box {
  if (!Array.isArray(value) || value.length !== 6 || !value.every((c) => typeof c === 'number')) {
    throw new Error(`${what}: not 6 numbers`)
  }

  return value.map(f32)
}

export function parseShapes(raw: Buffer): BlockShapes {
  const root = nbt.simplify(nbt.parseUncompressed(raw, 'big')) as { blocks?: unknown }

  if (!Array.isArray(root.blocks)) {
    throw new Error('block_shapes.nbt: no blocks list')
  }

  return {
    blocks: (root.blocks as { name?: unknown; states?: unknown }[]).map(({ name, states }, i) => {
      if (typeof name !== 'string' || !Array.isArray(states)) {
        throw new Error(`block_shapes.nbt: block ${i} is not { name, states }`)
      }

      return {
        name,
        states: (states as { collision?: unknown; visual?: unknown; ui?: unknown; outline?: unknown; camera?: unknown }[]).map(({ collision, visual, ui, outline, camera }, s) => {
          const at = `${name} #${s}`

          if (!Array.isArray(collision)) {
            throw new Error(`${at}: no collision boxes`)
          }

          const shapes: StateShapes = {
            collision: collision.map((b, n) => box(b, `${at} collision ${n}`)),
            visual: box(visual, `${at} visual`),
            ui: box(ui, `${at} ui`),
            outline: box(outline, `${at} outline`),
          }

          if (camera !== undefined) {
            if (!Array.isArray(camera) || camera.length > 1) {
              throw new Error(`${at}: camera is not none or one box`)
            }

            shapes.camera = camera.length ? box(camera[0], `${at} camera`) : null
          }

          return shapes
        }),
      }
    }),
  }
}

export function readAgentOutput(out: string): Blocks {
  const palette = readFileSync(join(out, PALETTE_FILE))
  const states = parsePalette(palette)
  const shapes = parseShapes(readFileSync(join(out, 'block_shapes.nbt')))
  checkPalette(states)
  checkShapes(shapes, states)
  return { palette, states, shapes }
}

export async function readBlocks(zip: string, build: string, platform: Platform, work: string): Promise<Blocks> {
  const host = agentHost(platform)
  const server = join(work, 'server')
  const out = join(work, 'out')
  await unpackServer(zip, server, host)
  await runAgent(server, build, host, out)
  return readAgentOutput(out)
}
