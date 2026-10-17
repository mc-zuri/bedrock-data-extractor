// A tracked release made supported, platform by platform: each platform's zip read and checked as extract would read it
// (nothing is written to bedrock-data), and each platform that passes marked "supported" in
// versions/release/<build>.json. The support workflow runs this for each new release, and opens a pull request for what
// it marked, an issue for what failed.
import { rmSync } from 'node:fs'
import { cacheDir, readRelease } from './extract.ts'
import { errorMessage } from './util.ts'
import type { VersionStore } from './versionStore.ts'
import { PLATFORMS, type Platform } from './versions.ts'

export interface Support {
  supported: Platform[]
  failed: { platform: Platform; error: string }[]
}

/** Each of `platforms` the release has a zip of, tried. */
export async function support(store: VersionStore, build: string, platforms: readonly Platform[], cache: string): Promise<Support> {
  const version = store.read('release', build)
  if (!version) throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
  const result: Support = { supported: [], failed: [] }
  for (const platform of PLATFORMS.filter((p) => platforms.includes(p) && version.binary[p])) {
    try {
      await readRelease(version, platform, cache)
      result.supported.push(platform)
    } catch (e) {
      result.failed.push({ platform, error: errorMessage(e) })
    }
  }
  rmSync(cacheDir(cache, build), { recursive: true, force: true })
  store.setSupported(build, result.supported)
  return result
}
