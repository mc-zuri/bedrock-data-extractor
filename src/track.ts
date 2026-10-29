import { hashZip } from './download.ts'
import type { VersionStore, VersionJson } from './versionStore.ts'
import { PLATFORMS, versionOf, zipUrl, type Platform, type Target } from './versions.ts'

/** A build to track, and the platforms the download links offer it for */
export interface Offer extends Target {
  platforms?: Platform[]
}

export function isTracked(store: VersionStore, t: Offer): boolean {
  const json = store.read(t.channel, t.build)
  return json !== undefined && Object.keys(json.binary).length > 0 && !t.platforms?.some((p) => !json.binary[p])
}

export async function track(store: VersionStore, t: Offer, force: boolean): Promise<string | undefined> {
  if (!force && isTracked(store, t)) {
    return undefined
  }

  const old = store.read(t.channel, t.build)
  const json: VersionJson = { build: t.build, version: versionOf(t.build, t.channel), channel: t.channel, binary: {} }

  for (const platform of PLATFORMS) {
    const url = zipUrl(t.build, t.channel, platform)
    const hashed = await hashZip(url)
    const was = old?.binary[platform]

    if (hashed) {
      json.binary[platform] = { url, ...hashed, supported: was?.supported === true && was.sha256 === hashed.sha256 }
    }
  }

  if (!Object.keys(json.binary).length) {
    throw new Error(`minecraft.net has no ${t.channel} zip of ${t.build} (${zipUrl(t.build, t.channel, 'linux')})`)
  }

  for (const platform of t.platforms ?? []) {
    if (!json.binary[platform]) {
      throw new Error(`the download links offer ${platform}'s ${t.build}, which is not at ${zipUrl(t.build, t.channel, platform)}`)
    }
  }

  return store.write(json) ? `hashed (${Object.keys(json.binary).join(', ')})` : 'unchanged'
}
