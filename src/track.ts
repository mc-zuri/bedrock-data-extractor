import { hashZip } from './download.ts'
import type { VersionStore, VersionJson } from './versionStore.ts'
import { PLATFORMS, versionOf, zipUrl, type Channel, type Platform } from './versions.ts'

export interface Target {
  channel: Channel
  build: string
  platforms?: Platform[]
}

export function targetName(t: Target): string {
  return `${t.channel}/${t.build}`
}

export function isTracked(store: VersionStore, t: Target): boolean {
  const json = store.read(t.channel, t.build)
  return json !== undefined && Object.keys(json.binary).length > 0 && !t.platforms?.some((p) => !json.binary[p])
}

export async function track(store: VersionStore, t: Target, force: boolean): Promise<string | undefined> {
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
