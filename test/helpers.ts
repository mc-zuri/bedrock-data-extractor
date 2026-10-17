import { createHash } from 'node:crypto'
import { cpSync, mkdirSync, mkdtempSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { crc32, deflateRawSync } from 'node:zlib'
import nbt from 'prismarine-nbt'
import type { Blocks, BlockShapes, BlockState } from '../src/blocks.ts'
import { BEDROCK_DATA_DIR, SCHEMAS_DIR } from '../src/config.ts'
import type { ReadBlocks } from '../src/extract.ts'
import { VersionStore, type VersionJson } from '../src/versionStore.ts'
import { zipUrl, type Platform } from '../src/versions.ts'

/** A zip of `files` (name -> content), each deflated: as a server zip is. */
export function writeZip(file: string, files: Record<string, string | Buffer>): void {
  const locals: Buffer[] = []
  const centrals: Buffer[] = []
  let offset = 0

  for (const [name, content] of Object.entries(files)) {
    const data = Buffer.isBuffer(content) ? content : Buffer.from(content, 'utf8')
    const packed = deflateRawSync(data)
    const fileName = Buffer.from(name, 'utf8')
    const local = Buffer.alloc(30)
    local.writeUInt32LE(0x04034b50, 0)
    local.writeUInt16LE(20, 4)
    local.writeUInt16LE(8, 8)
    local.writeUInt32LE(crc32(data), 14)
    local.writeUInt32LE(packed.length, 18)
    local.writeUInt32LE(data.length, 22)
    local.writeUInt16LE(fileName.length, 26)
    const central = Buffer.alloc(46)
    central.writeUInt32LE(0x02014b50, 0)
    central.writeUInt16LE(20, 4)
    central.writeUInt16LE(20, 6)
    central.writeUInt16LE(8, 10)
    central.writeUInt32LE(crc32(data), 16)
    central.writeUInt32LE(packed.length, 20)
    central.writeUInt32LE(data.length, 24)
    central.writeUInt16LE(fileName.length, 28)
    central.writeUInt32LE(offset, 42)
    locals.push(local, fileName, packed)
    centrals.push(central, fileName)
    offset += local.length + fileName.length + packed.length
  }

  const directory = Buffer.concat(centrals)
  const end = Buffer.alloc(22)
  end.writeUInt32LE(0x06054b50, 0)
  end.writeUInt16LE(Object.keys(files).length, 8)
  end.writeUInt16LE(Object.keys(files).length, 10)
  end.writeUInt32LE(directory.length, 12)
  end.writeUInt32LE(offset, 16)
  writeFileSync(file, Buffer.concat([...locals, directory, end]))
}

export function tempDir(): string {
  return mkdtempSync(join(tmpdir(), 'bedrock-data-extractor-'))
}

/** An empty bedrock-data checkout: only the real one's schemas (BEDROCK_DATA_DIR), which every write is checked against. */
export function emptyDataRepo(): string {
  const root = tempDir()
  cpSync(join(BEDROCK_DATA_DIR, 'schemas'), join(root, 'schemas'), { recursive: true })
  return root
}

/** A vanilla en_US.lang with as many strings as a release's has (checks.ts) */
export const VANILLA = Array.from({ length: 6000 }, (_, i) => `tile.block_${i}.name=Block ${i}`).join('\n')

/**
 * A tracked release whose zips are already in the cache, as finished downloads leave them: nothing is fetched.
 * `zips`: each platform's vanilla en_US.lang; `supported`: the platforms marked so; `wrongSha`: those tracked with
 * another zip's sha256.
 */
export function cachedRelease(
  build: string,
  zips: Partial<Record<Platform, string>>,
  supported: Platform[] = [],
  wrongSha: Platform[] = [],
): { store: VersionStore; cache: string } {
  const cache = tempDir()
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  const binary: VersionJson['binary'] = {}

  for (const [platform, vanilla] of Object.entries(zips) as [Platform, string][]) {
    const zip = join(cache, 'release', build, `${platform}.zip`)
    mkdirSync(dirname(zip), { recursive: true })
    writeZip(zip, { 'resource_packs/vanilla/texts/en_US.lang': vanilla, 'resource_packs/chemistry/texts/en_US.lang': 'item.element_1.name=Hydrogen\n' })
    const sha256 = wrongSha.includes(platform) ? 'd'.repeat(64) : createHash('sha256').update(readFileSync(zip)).digest('hex')
    binary[platform] = { url: zipUrl(build, 'release', platform), sha256, size: statSync(zip).size, supported: supported.includes(platform) }
  }

  store.write({ build, version: build.split('.').slice(0, 3).join('.'), channel: 'release', binary })
  return { store, cache }
}

type NbtState = BlockState['states']

/** `states` as the agent writes the palette: big-endian NBT { blocks: [{ name, states, version }] } */
export function paletteNbt(states: BlockState[]): Buffer {
  const entries = states.map((s) => ({ name: { type: 'string', value: s.name }, states: { type: 'compound', value: s.states }, version: { type: 'int', value: s.version } }))
  return nbt.writeUncompressed({ type: 'compound', name: '', value: { blocks: { type: 'list', value: { type: 'compound', value: entries } } } } as nbt.NBT, 'big')
}

/** `shapes` as the agent writes them: big-endian NBT { blocks: [{ name, states: [{ collision, visual, ui, outline, camera? }] }] } */
export function shapesNbt(shapes: BlockShapes): Buffer {
  function box(b: number[]): { type: string; value: number[] } {
    return { type: 'float', value: b }
  }
  const blocks = shapes.blocks.map((block) => ({
    name: { type: 'string', value: block.name },
    states: {
      type: 'list',
      value: {
        type: 'compound',
        value: block.states.map((s) => ({
          collision: { type: 'list', value: { type: 'list', value: s.collision.map(box) } },
          visual: { type: 'list', value: box(s.visual) },
          ui: { type: 'list', value: box(s.ui) },
          outline: { type: 'list', value: box(s.outline) },
          ...(s.camera === undefined ? {} : { camera: { type: 'list', value: { type: 'list', value: s.camera ? [box(s.camera)] : [] } } }),
        })),
      },
    },
  }))
  return nbt.writeUncompressed({ type: 'compound', name: '', value: { blocks: { type: 'list', value: { type: 'compound', value: blocks } } } } as nbt.NBT, 'big')
}

const CUBE = [0, 0, 0, 1, 1, 1]

/** A palette the checks pass (air, stone, and a block of 500 states) and its shapes. */
export function sampleBlocks(): Blocks {
  const color: NbtState[] = Array.from({ length: 500 }, (_, i) => ({ color: { type: 'int', value: i } }))
  const states: BlockState[] = [
    { name: 'minecraft:air', states: {}, version: 18168865 },
    { name: 'minecraft:stone', states: {}, version: 18168865 },
    ...color.map((s) => ({ name: 'minecraft:wool', states: s, version: 18168865 })),
  ]
  const shapes: BlockShapes = {
    blocks: [
      { name: 'minecraft:air', states: [{ collision: [], visual: [0, 0, 0, 0, 0, 0], ui: [0, 0, 0, 0, 0, 0], outline: [0, 0, 0, 0, 0, 0], camera: null }] },
      { name: 'minecraft:stone', states: [{ collision: [CUBE], visual: CUBE, ui: CUBE, outline: CUBE, camera: CUBE }] },
      {
        name: 'minecraft:wool',
        states: color.map(() => ({ collision: [[0.025, 0, 0.025, 0.975, 0.95000005, 0.975]], visual: CUBE, ui: CUBE, outline: CUBE, camera: CUBE })),
      },
    ],
  }
  return { palette: paletteNbt(states), states, shapes }
}

/** What reads a release's blocks, where no server runs: `answer` for every zip (an Error: thrown). Each call is noted in `calls`. */
export function fakeBlocks(answer: Blocks | Error = sampleBlocks()): ReadBlocks & { calls: { build: string; platform: Platform }[] } {
  const calls: { build: string; platform: Platform }[] = []
  async function read(_zip: string, build: string, platform: Platform): Promise<Blocks> {
    calls.push({ build, platform })
    await Promise.resolve()

    if (answer instanceof Error) {
      throw answer
    }

    return answer
  }
  return Object.assign(read, { calls })
}
