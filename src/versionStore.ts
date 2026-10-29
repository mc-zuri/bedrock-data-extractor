import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { keepJson, Schemas } from './files.ts'
import { CHANNELS, compareVersions, isBuild, PLATFORMS, targetName, trackCommand, type Channel, type Platform, type Target } from './versions.ts'

interface Binary {
  url: string
  sha256: string
  size: number
  supported: boolean
}

export interface VersionJson {
  build: string
  version: string
  channel: Channel
  binary: Partial<Record<Platform, Binary>>
}

export class VersionStore {
  readonly root: string
  private readonly schemas: Schemas

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

  builds(channel: Channel): string[] {
    const dir = join(this.root, channel)

    if (!existsSync(dir)) {
      return []
    }

    return readdirSync(dir)
      .map((name) => name.replace(/\.json$/, ''))
      .filter(isBuild)
      .sort((a, b) => compareVersions(b, a))
  }

  /** The builds supported on some platform: the releases, then the previews, each newest first */
  supportedBuilds(): Target[] {
    return CHANNELS.flatMap((channel) =>
      this.builds(channel)
        .filter((build) => {
          const json = this.read(channel, build)
          return PLATFORMS.some((p) => json?.binary[p]?.supported)
        })
        .map((build) => ({ channel, build })),
    )
  }

  /** `build` of `channel`, or of the one channel that tracks it */
  target(build: string, channel?: Channel): Target {
    const channels = channel ? [channel] : CHANNELS.filter((c) => this.read(c, build))

    if (channels.length > 1) {
      throw new Error(`${build} is tracked as a release and as a preview: name its channel (--channel=release|preview)`)
    }

    if (!channels.length || !this.read(channels[0], build)) {
      throw new Error(`${channel ? `${channel}/` : ''}${build} is not tracked: ${trackCommand({ channel: channel ?? 'release', build })}`)
    }

    return { channel: channels[0], build }
  }

  setSupported(t: Target, marks: Partial<Record<Platform, boolean>>): boolean {
    const json = this.read(t.channel, t.build)

    if (!json) {
      throw new Error(`${targetName(t)} is not tracked: ${trackCommand(t)}`)
    }

    for (const platform of PLATFORMS) {
      const binary = json.binary[platform]
      const supported = marks[platform]

      if (binary && supported !== undefined) {
        binary.supported = supported
      }
    }

    return this.write(json)
  }

  write(json: VersionJson): boolean {
    this.schemas.check('version', json, `versions/${json.channel}/${json.build}.json`)
    return keepJson(this.file(json.channel, json.build), json)
  }

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
