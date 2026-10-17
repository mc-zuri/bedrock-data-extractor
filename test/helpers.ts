import { createHash } from 'node:crypto'
import { cpSync, mkdirSync, mkdtempSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { dirname, join } from 'node:path'
import { crc32, deflateRawSync } from 'node:zlib'
import { BEDROCK_DATA_DIR, SCHEMAS_DIR } from '../src/config.ts'
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
