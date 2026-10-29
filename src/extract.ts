import { rmSync } from 'node:fs'
import { join } from 'node:path'
import { asOnWindows, BLOCKS_FILE, ENCHANTMENTS_FILE, ITEMS_FILE, PALETTE_FILE, readBlocks, SHAPES_FILE, type Blocks } from './blocks.ts'
import { checkLanguage } from './checks.ts'
import { KINDS, type BuildJson, type DataRepo, type Kind } from './dataRepo.ts'
import { download } from './download.ts'
import { LANGUAGE_FILE, readLanguage, type Language } from './language.ts'
import type { VersionJson, VersionStore } from './versionStore.ts'
import { PLATFORMS, targetName, trackCommand, type Platform, type Target } from './versions.ts'

export type ReadBlocks = (zip: string, t: Target, platform: Platform, work: string, signal?: AbortSignal) => Promise<Blocks>

export interface Release {
  language: Language
  blocks: Blocks
}

export function exportPlatform(version: VersionJson): Platform | undefined {
  return PLATFORMS.find((p) => version.binary[p]?.supported)
}

export function isExtracted(repo: DataRepo, t: Target): boolean {
  const data = repo.readBuild(t)?.data
  const kinds = [
    data?.language,
    data?.blockPalette,
    data?.blockShapes,
    data?.blocks,
    data?.items,
    data?.enchantments,
    ...(Object.keys(KINDS) as Kind[]).map((kind) => data?.[kind]),
  ]
  return kinds.every((kind) => kind && repo.has(t, kind.file))
}

export function cacheDir(cache: string, t: Target): string {
  return join(cache, t.channel, t.build)
}

export async function trackedZip(version: VersionJson, platform: Platform, cache: string): Promise<string> {
  const tracked = version.binary[platform]

  if (!tracked) {
    throw new Error(`${targetName(version)} has no ${platform} zip`)
  }

  const file = join(cacheDir(cache, version), `${platform}.zip`)
  const zip = await download(tracked.url, file)

  if (!zip) {
    throw new Error(`${tracked.url} is gone`)
  }

  if (zip.sha256 !== tracked.sha256) {
    rmSync(file, { force: true })
    throw new Error(`${tracked.url} is not the zip tracked: sha256 ${zip.sha256}, not ${tracked.sha256} (${trackCommand(version)} --force, if minecraft.net changed it)`)
  }

  return zip.file
}

export function workDir(cache: string, t: Target, platform: Platform): string {
  return join(cacheDir(cache, t), platform)
}

export async function readRelease(version: VersionJson, platform: Platform, cache: string, blocks: ReadBlocks = readBlocks, signal?: AbortSignal): Promise<Release> {
  const zip = await trackedZip(version, platform, cache)
  const other = PLATFORMS.find((p) => p !== platform && version.binary[p])
  const language = await readLanguage(zip, other ? () => trackedZip(version, other, cache) : undefined)
  checkLanguage(language)
  const read = await blocks(zip, version, platform, workDir(cache, version, platform), signal)
  return { language, blocks: asOnWindows(read, version.build, platform) }
}

export async function extract(
  repo: DataRepo,
  store: VersionStore,
  t: Target,
  force: boolean,
  cache: string,
  blocks: ReadBlocks = readBlocks,
  signal?: AbortSignal,
): Promise<string | undefined> {
  if (!force && isExtracted(repo, t)) {
    return undefined
  }

  const version = store.read(t.channel, t.build)

  if (!version) {
    throw new Error(`${targetName(t)} is not tracked: ${trackCommand(t)}`)
  }

  const from = exportPlatform(version)

  if (!from) {
    throw new Error(`${targetName(t)} is supported on no platform: pnpm support ${t.build}`)
  }

  const release = await readRelease(version, from, cache, blocks, signal)
  const changed: string[] = []
  const binary: BuildJson['binary'] = {}

  if (repo.writeLanguage(t, release.language)) {
    changed.push(LANGUAGE_FILE)
  }

  if (repo.writePalette(t, release.blocks.palette)) {
    changed.push(PALETTE_FILE)
  }

  if (repo.writeShapes(t, release.blocks.shapes)) {
    changed.push(SHAPES_FILE)
  }

  if (repo.writeBlocks(t, release.blocks.blocks)) {
    changed.push(BLOCKS_FILE)
  }

  if (repo.writeItems(t, release.blocks.items)) {
    changed.push(ITEMS_FILE)
  }

  if (repo.writeEnchantments(t, release.blocks.enchantments)) {
    changed.push(ENCHANTMENTS_FILE)
  }

  for (const kind of Object.keys(KINDS) as Kind[]) {
    if (repo.writeKind(t, kind, release.blocks[kind])) {
      changed.push(KINDS[kind].file)
    }
  }

  for (const platform of PLATFORMS) {
    const zip = version.binary[platform]

    if (zip) {
      binary[platform] = { url: zip.url, sha256: zip.sha256, size: zip.size }
    }
  }

  // a steve recorded before stays: extract does not record it
  const steve = repo.readBuild(t)?.data.steve
  const json: BuildJson = {
    build: t.build,
    version: version.version,
    channel: t.channel,
    binary,
    data: {
      language: {
        file: LANGUAGE_FILE,
        from,
      },
      blockPalette: { file: PALETTE_FILE, from },
      blockShapes: { file: SHAPES_FILE, from },
      blocks: { file: BLOCKS_FILE, from },
      items: { file: ITEMS_FILE, from },
      enchantments: { file: ENCHANTMENTS_FILE, from },
      ...Object.fromEntries((Object.keys(KINDS) as Kind[]).map((kind) => [kind, { file: KINDS[kind].file, from }])),
      ...(steve ? { steve } : {}),
    },
  }

  if (repo.writeBuild(json)) {
    changed.unshift('build.json')
  }

  rmSync(cacheDir(cache, t), { recursive: true, force: true })
  const strings = Object.keys(release.language.packs.vanilla).length

  return `${changed.length ? `${changed.join(', ')} written` : 'unchanged'} (from ${from}: ${strings} vanilla strings, ${release.blocks.states.length} block states)`
}
