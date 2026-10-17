// The bedrock-data checkout: versions.json, <channel>/<build>/build.json and the data extracted from each release.
// Each file is checked against the checkout's own schemas before it is written, and written only where what it says
// changed: a run that finds nothing new leaves the checkout as it is, on any machine.
import { Ajv, type ValidateFunction } from 'ajv'
import { existsSync, mkdirSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { gunzipSync, gzipSync } from 'node:zlib'
import { LANGUAGE_FILE, type Language } from './language.ts'
import { readIfExists, writeAtomic } from './util.ts'
import { CHANNELS, compareVersions, isBuild, type Channel, type Platform } from './versions.ts'

export const FORMAT_VERSION = 1

export interface Binary {
  url: string
  sha256: string
  size: number
}

export interface BuildJson {
  build: string
  version: string
  channel: Channel
  binary: Partial<Record<Platform, Binary>>
  data?: {
    language?: { file: typeof LANGUAGE_FILE; from: Platform }
  }
}

interface ChannelBuilds {
  latest?: string
  builds: string[]
}

export interface VersionsJson {
  formatVersion: typeof FORMAT_VERSION
  release: ChannelBuilds
  preview: ChannelBuilds
}

type Schema = 'versions' | 'build' | 'language'

export class DataRepo {
  readonly root: string
  private readonly validators = new Map<Schema, ValidateFunction>()

  constructor(root: string) {
    this.root = root
  }

  buildDir(channel: Channel, build: string): string {
    return join(this.root, channel, build)
  }

  /** The schema of bedrock-data's own schemas/, compiled once. */
  private check(schema: Schema, value: unknown, what: string): void {
    let validate = this.validators.get(schema)
    if (!validate) {
      const file = join(this.root, 'schemas', `${schema}.schema.json`)
      if (!existsSync(file)) throw new Error(`no ${file}: is ${this.root} a bedrock-data checkout?`)
      validate = new Ajv({ allErrors: true }).compile(JSON.parse(readFileSync(file, 'utf8')) as object)
      this.validators.set(schema, validate)
    }
    if (!validate(value)) throw new Error(`${what} is not as schemas/${schema}.schema.json says: ${JSON.stringify(validate.errors?.slice(0, 3))}`)
  }

  readBuild(channel: Channel, build: string): BuildJson | undefined {
    const text = readIfExists(join(this.buildDir(channel, build), 'build.json'))
    return text === undefined ? undefined : (JSON.parse(text.toString('utf8')) as BuildJson)
  }

  /** The builds of `channel` with a build.json, newest first. */
  builds(channel: Channel): string[] {
    const dir = join(this.root, channel)
    if (!existsSync(dir)) return []
    return readdirSync(dir)
      .filter((build) => isBuild(build) && existsSync(join(dir, build, 'build.json')))
      .sort((a, b) => compareVersions(b, a))
  }

  /** Returns whether it changed. */
  writeBuild(json: BuildJson): boolean {
    this.check('build', json, `${json.channel}/${json.build}/build.json`)
    return keepText(join(this.buildDir(json.channel, json.build), 'build.json'), `${JSON.stringify(json, null, 2)}\n`)
  }

  /** Returns whether it changed. */
  writeLanguage(build: string, language: Language): boolean {
    this.check('language', language, `release/${build}/${LANGUAGE_FILE}`)
    return keepGzipped(join(this.buildDir('release', build), LANGUAGE_FILE), `${JSON.stringify(language)}\n`)
  }

  hasLanguage(build: string): boolean {
    return existsSync(join(this.buildDir('release', build), LANGUAGE_FILE))
  }

  /** versions.json, from the build.json files there are. Returns whether it changed. */
  writeVersions(): boolean {
    const versions: VersionsJson = { formatVersion: FORMAT_VERSION, release: { builds: [] }, preview: { builds: [] } }
    for (const channel of CHANNELS) {
      const builds = this.builds(channel)
      versions[channel] = builds.length ? { latest: builds[0], builds } : { builds }
    }
    this.check('versions', versions, 'versions.json')
    return keepText(join(this.root, 'versions.json'), `${JSON.stringify(versions, null, 2)}\n`)
  }
}

function keepText(file: string, text: string): boolean {
  if (readIfExists(file)?.toString('utf8') === text) return false
  mkdirSync(join(file, '..'), { recursive: true })
  writeAtomic(file, text)
  return true
}

/**
 * gzip at level 9 with no name and no time, and "unknown" for the OS (zlib writes the one it was built on). The same
 * text can still come out as other bytes from another zlib, so a file is rewritten only where what it decompresses
 * to changed.
 */
export function gzip(text: string): Buffer {
  const out = gzipSync(text, { level: 9 })
  out[9] = 0xff
  return out
}

function keepGzipped(file: string, text: string): boolean {
  const old = readIfExists(file)
  if (old !== undefined && gunzipSync(old).toString('utf8') === text) return false
  mkdirSync(join(file, '..'), { recursive: true })
  writeAtomic(file, gzip(text))
  return true
}
