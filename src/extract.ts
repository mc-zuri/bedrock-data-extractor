// A tracked release into bedrock-data: its data read out of one of its zips (linux's where both have one), checked to
// be the zip versions/ hashed, and its version file with what was extracted as build.json. Its zip is deleted once it
// is done; a build that failed keeps it, for the next try.
import { rmSync } from 'node:fs'
import { join } from 'node:path'
import type { BuildJson, DataRepo } from './dataRepo.ts'
import { download } from './download.ts'
import { LANGUAGE_FILE, readLanguage } from './language.ts'
import type { VersionJson, VersionStore } from './versionStore.ts'
import { PLATFORMS } from './versions.ts'

/** Extracted from the zips versions/ has for it now, with all a release's data. */
export function isExtracted(repo: DataRepo, version: VersionJson): boolean {
  const json = repo.readBuild(version.build)
  return json !== undefined && JSON.stringify(json.binary) === JSON.stringify(version.binary) && json.data.language !== undefined && repo.hasLanguage(version.build)
}

/** What changed, undefined where it was extracted already (and not `force`d). `cache`: where its zip is downloaded to. */
export async function extract(repo: DataRepo, store: VersionStore, build: string, force: boolean, cache: string): Promise<string | undefined> {
  const version = store.read('release', build)
  if (!version) throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
  if (!force && isExtracted(repo, version)) return undefined
  const from = PLATFORMS.find((p) => version.binary[p])
  const tracked = from && version.binary[from]
  if (!from || !tracked) throw new Error(`versions/release/${build}.json has no zip`)
  const dir = join(cache, 'release', build)
  const zip = await download(tracked.url, join(dir, `${from}.zip`))
  if (!zip) throw new Error(`${tracked.url} is gone`)
  if (zip.sha256 !== tracked.sha256) {
    rmSync(dir, { recursive: true, force: true })
    throw new Error(`${tracked.url} is not the zip tracked: sha256 ${zip.sha256}, not ${tracked.sha256} (pnpm track ${build} --force, if minecraft.net changed it)`)
  }
  const changed: string[] = []
  if (repo.writeLanguage(build, await readLanguage(zip.file))) changed.push(LANGUAGE_FILE)
  const json: BuildJson = { ...version, channel: 'release', data: { language: { file: LANGUAGE_FILE, from } } }
  if (repo.writeBuild(json)) changed.unshift('build.json')
  rmSync(dir, { recursive: true, force: true })
  return changed.length ? `${changed.join(', ')} written (from ${from})` : 'unchanged'
}
