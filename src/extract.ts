// A supported release into bedrock-data: its data read out of the zip of a platform it is supported on (linux's where
// both are), checked to be the zip versions/ hashed and to hold what it must (checks.ts), and its version file with what
// was extracted as build.json. A release bedrock-data has the data of is left as it is. Its zip is deleted once it is
// done; a build that failed keeps it, for the next try.
import { rmSync } from 'node:fs'
import { join } from 'node:path'
import { checkLanguage } from './checks.ts'
import type { BuildJson, DataRepo } from './dataRepo.ts'
import { download } from './download.ts'
import { LANGUAGE_FILE, readLanguage, type Language } from './language.ts'
import type { VersionJson, VersionStore } from './versionStore.ts'
import { PLATFORMS, type Platform } from './versions.ts'

/** bedrock-data has all a release's data. */
export function isExtracted(repo: DataRepo, build: string): boolean {
  return repo.readBuild(build)?.data.language !== undefined && repo.hasLanguage(build)
}

export function cacheDir(cache: string, build: string): string {
  return join(cache, 'release', build)
}

/** A release's data out of `platform`'s zip (downloaded into `cache`, where it is left), checked. */
export async function readRelease(version: VersionJson, platform: Platform, cache: string): Promise<Language> {
  const tracked = version.binary[platform]
  if (!tracked) throw new Error(`release ${version.build} has no ${platform} zip`)
  const file = join(cacheDir(cache, version.build), `${platform}.zip`)
  const zip = await download(tracked.url, file)
  if (!zip) throw new Error(`${tracked.url} is gone`)
  if (zip.sha256 !== tracked.sha256) {
    rmSync(file, { force: true })
    throw new Error(`${tracked.url} is not the zip tracked: sha256 ${zip.sha256}, not ${tracked.sha256} (pnpm track ${version.build} --force, if minecraft.net changed it)`)
  }
  const language = await readLanguage(zip.file)
  checkLanguage(language)
  return language
}

/** What changed, undefined where bedrock-data has its data already (and not `force`d). `cache`: where its zip is downloaded to. */
export async function extract(repo: DataRepo, store: VersionStore, build: string, force: boolean, cache: string): Promise<string | undefined> {
  if (!force && isExtracted(repo, build)) return undefined
  const version = store.read('release', build)
  if (!version) throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
  const from = PLATFORMS.find((p) => version.binary[p]?.supported)
  if (!from) throw new Error(`release ${build} is supported on no platform: pnpm support ${build}`)
  const language = await readRelease(version, from, cache)
  const changed: string[] = []
  if (repo.writeLanguage(build, language)) changed.push(LANGUAGE_FILE)
  // bedrock-data keeps where each zip is, not what this extractor supports
  const binary: BuildJson['binary'] = {}
  for (const platform of PLATFORMS) {
    const zip = version.binary[platform]
    if (zip) binary[platform] = { url: zip.url, sha256: zip.sha256, size: zip.size }
  }
  const json: BuildJson = { build, version: version.version, channel: 'release', binary, data: { language: { file: LANGUAGE_FILE, from } } }
  if (repo.writeBuild(json)) changed.unshift('build.json')
  rmSync(cacheDir(cache, build), { recursive: true, force: true })
  return `${changed.length ? `${changed.join(', ')} written` : 'unchanged'} (from ${from}: ${Object.keys(language.packs.vanilla).length} vanilla strings)`
}
