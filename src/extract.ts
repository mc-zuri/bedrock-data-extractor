import { rmSync } from 'node:fs'
import { join } from 'node:path'
import { PALETTE_FILE, readBlocks, SHAPES_FILE, type Blocks } from './blocks.ts'
import { checkLanguage } from './checks.ts'
import type { BuildJson, DataRepo } from './dataRepo.ts'
import { download } from './download.ts'
import { LANGUAGE_FILE, readLanguage, type Language } from './language.ts'
import type { VersionJson, VersionStore } from './versionStore.ts'
import { PLATFORMS, type Platform } from './versions.ts'

export type ReadBlocks = (zip: string, build: string, platform: Platform, work: string) => Promise<Blocks>

export interface Release {
  language: Language
  blocks: Blocks
}

export function exportPlatform(version: VersionJson): Platform | undefined {
  return PLATFORMS.find((p) => version.binary[p]?.supported)
}

export function isExtracted(repo: DataRepo, build: string): boolean {
  const data = repo.readBuild(build)?.data
  return [data?.language, data?.blockPalette, data?.blockShapes].every((kind) => kind && repo.has(build, kind.file))
}

export function cacheDir(cache: string, build: string): string {
  return join(cache, 'release', build)
}

export async function trackedZip(version: VersionJson, platform: Platform, cache: string): Promise<string> {
  const tracked = version.binary[platform]

  if (!tracked) {
    throw new Error(`release ${version.build} has no ${platform} zip`)
  }

  const file = join(cacheDir(cache, version.build), `${platform}.zip`)
  const zip = await download(tracked.url, file)

  if (!zip) {
    throw new Error(`${tracked.url} is gone`)
  }

  if (zip.sha256 !== tracked.sha256) {
    rmSync(file, { force: true })
    throw new Error(`${tracked.url} is not the zip tracked: sha256 ${zip.sha256}, not ${tracked.sha256} (pnpm track ${version.build} --force, if minecraft.net changed it)`)
  }

  return zip.file
}

export function workDir(cache: string, build: string, platform: Platform): string {
  return join(cacheDir(cache, build), platform)
}

export async function readRelease(version: VersionJson, platform: Platform, cache: string, blocks: ReadBlocks = readBlocks): Promise<Release> {
  const zip = await trackedZip(version, platform, cache)
  const language = await readLanguage(zip)
  checkLanguage(language)
  return {
    language,
    blocks: await blocks(zip, version.build, platform, workDir(cache, version.build, platform)),
  }
}

export async function extract(repo: DataRepo, store: VersionStore, build: string, force: boolean, cache: string, blocks: ReadBlocks = readBlocks): Promise<string | undefined> {
  if (!force && isExtracted(repo, build)) {
    return undefined
  }

  const version = store.read('release', build)

  if (!version) {
    throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
  }

  const from = exportPlatform(version)

  if (!from) {
    throw new Error(`release ${build} is supported on no platform: pnpm support ${build}`)
  }

  const release = await readRelease(version, from, cache, blocks)
  const changed: string[] = []
  const binary: BuildJson['binary'] = {}

  if (repo.writeLanguage(build, release.language)) {
    changed.push(LANGUAGE_FILE)
  }

  if (repo.writePalette(build, release.blocks.palette)) {
    changed.push(PALETTE_FILE)
  }

  if (repo.writeShapes(build, release.blocks.shapes)) {
    changed.push(SHAPES_FILE)
  }

  for (const platform of PLATFORMS) {
    const zip = version.binary[platform]

    if (zip) {
      binary[platform] = { url: zip.url, sha256: zip.sha256, size: zip.size }
    }
  }

  const json: BuildJson = {
    build,
    version: version.version,
    channel: 'release',
    binary,
    data: {
      language: {
        file: LANGUAGE_FILE,
        from,
      },
      blockPalette: { file: PALETTE_FILE, from },
      blockShapes: { file: SHAPES_FILE, from },
    },
  }

  if (repo.writeBuild(json)) {
    changed.unshift('build.json')
  }

  rmSync(cacheDir(cache, build), { recursive: true, force: true })
  const strings = Object.keys(release.language.packs.vanilla).length

  return `${changed.length ? `${changed.join(', ')} written` : 'unchanged'} (from ${from}: ${strings} vanilla strings, ${release.blocks.states.length} block states)`
}
