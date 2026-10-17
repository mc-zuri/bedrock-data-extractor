// A build into bedrock-data: each platform's zip downloaded and hashed (build.json), and for a release its data read
// out of one of them (linux's where both have it). Its zips are deleted once it is done; a build that failed keeps
// those it downloaded, for the next try.
import { rmSync } from 'node:fs'
import { join } from 'node:path'
import { CACHE_DIR } from './config.ts'
import type { BuildJson, DataRepo } from './dataRepo.ts'
import { download, type Zip } from './download.ts'
import { LANGUAGE_FILE, readLanguage } from './language.ts'
import { PLATFORMS, versionOf, zipUrl, type Channel, type Platform } from './versions.ts'

export interface Target {
  channel: Channel
  build: string
  /** the platforms minecraft.net is known to offer it for (the download links'); unknown for a build named by hand */
  platforms?: Platform[]
}

export function targetName(t: Target): string {
  return `${t.channel}/${t.build}`
}

/** Kept with all it should have: each known platform's download, and a release's data. */
export function isComplete(repo: DataRepo, t: Target): boolean {
  const json = repo.readBuild(t.channel, t.build)
  if (!json || !Object.keys(json.binary).length) return false
  if (t.platforms?.some((p) => !json.binary[p])) return false
  return t.channel === 'preview' || (json.data?.language !== undefined && repo.hasLanguage(t.build))
}

/** What changed, undefined where it was complete already (and not `force`d). */
export async function extract(repo: DataRepo, t: Target, force: boolean): Promise<string | undefined> {
  if (!force && isComplete(repo, t)) return undefined
  const cache = join(CACHE_DIR, t.channel, t.build)
  const zips = new Map<Platform, Zip>()
  for (const platform of PLATFORMS) {
    const zip = await download(zipUrl(t.build, t.channel, platform), join(cache, `${platform}.zip`))
    if (zip) zips.set(platform, zip)
  }
  if (!zips.size) throw new Error(`minecraft.net has no ${t.channel} zip of ${t.build} (${zipUrl(t.build, t.channel, 'linux')})`)
  for (const platform of t.platforms ?? [])
    if (!zips.has(platform)) throw new Error(`the download links offer ${platform}'s ${t.build}, which is not at ${zipUrl(t.build, t.channel, platform)}`)

  const json: BuildJson = { build: t.build, version: versionOf(t.build, t.channel), channel: t.channel, binary: {} }
  for (const [platform, zip] of zips) json.binary[platform] = { url: zipUrl(t.build, t.channel, platform), sha256: zip.sha256, size: zip.size }
  const changed: string[] = []
  if (t.channel === 'release') {
    // zips is in PLATFORMS' order: linux's where there is one
    const [from, zip] = [...zips][0]
    if (repo.writeLanguage(t.build, await readLanguage(zip.file))) changed.push(LANGUAGE_FILE)
    json.data = { language: { file: LANGUAGE_FILE, from } }
  }
  if (repo.writeBuild(json)) changed.unshift('build.json')
  rmSync(cache, { recursive: true, force: true })
  return changed.length ? `${changed.join(', ')} written (${[...zips.keys()].join(', ')})` : 'unchanged'
}
