import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { keepJson, Schemas } from './files.ts'
import { CHANNELS, compareVersions, isBuild, PLATFORMS, type Channel, type Platform } from './versions.ts'

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

  supportedBuilds(): string[] {
    return this.builds('release').filter((build) => {
      const json = this.read('release', build)
      return PLATFORMS.some((p) => json?.binary[p]?.supported)
    })
  }

  setSupported(build: string, marks: Partial<Record<Platform, boolean>>): boolean {
    const json = this.read('release', build)

    if (!json) {
      throw new Error(`release ${build} is not tracked: pnpm track ${build}`)
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
