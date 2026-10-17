import { rmSync } from 'node:fs'
import { readBlocks } from './blocks.ts'
import { cacheDir, readRelease, type ReadBlocks } from './extract.ts'
import { errorMessage } from './util.ts'
import type { VersionStore } from './versionStore.ts'
import { PLATFORMS, type Platform } from './versions.ts'

export interface Support {
  supported: Platform[]
  failed: { platform: Platform; error: string }[]
}

export async function support(store: VersionStore, build: string, platforms: readonly Platform[], cache: string, blocks: ReadBlocks = readBlocks): Promise<Support> {
  const version = store.read('release', build)

  if (!version) {
    throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
  }

  const result: Support = { supported: [], failed: [] }

  for (const platform of PLATFORMS.filter((p) => platforms.includes(p) && version.binary[p])) {
    try {
      await readRelease(version, platform, cache, blocks)
      result.supported.push(platform)
    } catch (e) {
      result.failed.push({ platform, error: errorMessage(e) })
    }
  }

  if (!result.failed.length) {
    rmSync(cacheDir(cache, build), { recursive: true, force: true })
  }

  const marks: Partial<Record<Platform, boolean>> = {}

  for (const platform of result.supported) {
    marks[platform] = true
  }

  for (const { platform } of result.failed) {
    marks[platform] = false
  }

  store.setSupported(build, marks)
  return result
}
