// The builds minecraft.net has offered, kept in this repo (versions/, as EndstoneMC/bedrock-server-data keeps them):
// each build's downloads in versions/<channel>/<build>.json, and versions/versions.json, each channel's builds newest
// first. pnpm track writes them; pnpm support marks the platforms a release is exported from (support.ts).
import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { keepJson, Schemas } from './files.ts'
import { CHANNELS, compareVersions, isBuild, PLATFORMS, type Channel, type Platform } from './versions.ts'

interface Binary {
  url: string
  sha256: string
  size: number
  /** the export works from this zip (pnpm support): only a supported platform's zip is exported */
  supported: boolean
}

/** A build, and each platform's zip of it */
export interface VersionJson {
  build: string
  version: string
  channel: Channel
  binary: Partial<Record<Platform, Binary>>
}

export class VersionStore {
  readonly root: string
  private readonly schemas: Schemas

  /** `root`: versions/; `schemas`: the folder of version.schema.json and versions.schema.json */
  constructor(root: string, schemas: string) {
    this.root = root
    this.schemas = new Schemas(schemas)
  }

  private file(channel: Channel, build: string): string {
    return join(this.root, channel, `${build}.json`)
  }

  read(channel: Channel, build: string): VersionJson | undefined {
    const file = this.file(channel, build)
    return existsSync(file) ? (JSON.parse(readFileSync(file, 'utf8')) as VersionJson) : undefined
  }

  /** The builds of `channel` kept, newest first. */
  builds(channel: Channel): string[] {
    const dir = join(this.root, channel)
    if (!existsSync(dir)) return []
    return readdirSync(dir)
      .map((name) => name.replace(/\.json$/, ''))
      .filter(isBuild)
      .sort((a, b) => compareVersions(b, a))
  }

  /** The releases supported on some platform, newest first. */
  supportedBuilds(): string[] {
    return this.builds('release').filter((build) => {
      const json = this.read('release', build)
      return PLATFORMS.some((p) => json?.binary[p]?.supported)
    })
  }

  /** `platforms` of the release marked supported. Returns whether it changed. */
  setSupported(build: string, platforms: Platform[]): boolean {
    const json = this.read('release', build)
    if (!json) throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
    for (const platform of platforms) {
      const binary = json.binary[platform]
      if (binary) binary.supported = true
    }
    return this.write(json)
  }

  /** Returns whether it changed. */
  write(json: VersionJson): boolean {
    this.schemas.check('version', json, `versions/${json.channel}/${json.build}.json`)
    return keepJson(this.file(json.channel, json.build), json)
  }

  /** versions.json, from the build files there are. Returns whether it changed. */
  writeIndex(): boolean {
    const index: Record<string, { latest?: string; builds: string[] }> = {}
    for (const channel of CHANNELS) {
      const builds = this.builds(channel)
      index[channel] = builds.length ? { latest: builds[0], builds } : { builds }
    }
    this.schemas.check('versions', index, 'versions/versions.json')
    return keepJson(join(this.root, 'versions.json'), index)
  }
}
