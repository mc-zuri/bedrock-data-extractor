// The bedrock-data checkout: what was extracted from each release (release/<build>/), and versions.json, the releases
// it has. Each file is checked against the checkout's own schemas before it is written, and written only where what it
// says changed.
import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { keepGzipped, keepJson, Schemas } from './files.ts'
import { LANGUAGE_FILE, type Language } from './language.ts'
import type { VersionJson } from './versionStore.ts'
import { compareVersions, isBuild, type Platform } from './versions.ts'

export const FORMAT_VERSION = 1

/** A release's version file (versions/release/<build>.json), and what was extracted from which of its zips */
export interface BuildJson extends VersionJson {
  channel: 'release'
  data: {
    language?: { file: typeof LANGUAGE_FILE; from: Platform }
  }
}

export class DataRepo {
  readonly root: string
  private readonly schemas: Schemas

  constructor(root: string) {
    this.root = root
    this.schemas = new Schemas(join(root, 'schemas'))
  }

  buildDir(build: string): string {
    return join(this.root, 'release', build)
  }

  readBuild(build: string): BuildJson | undefined {
    const file = join(this.buildDir(build), 'build.json')
    return existsSync(file) ? (JSON.parse(readFileSync(file, 'utf8')) as BuildJson) : undefined
  }

  /** The releases with a build.json, newest first. */
  builds(): string[] {
    const dir = join(this.root, 'release')
    if (!existsSync(dir)) return []
    return readdirSync(dir)
      .filter((build) => isBuild(build) && existsSync(join(dir, build, 'build.json')))
      .sort((a, b) => compareVersions(b, a))
  }

  /** Returns whether it changed. */
  writeBuild(json: BuildJson): boolean {
    this.schemas.check('build', json, `release/${json.build}/build.json`)
    return keepJson(join(this.buildDir(json.build), 'build.json'), json)
  }

  /** Returns whether it changed. */
  writeLanguage(build: string, language: Language): boolean {
    this.schemas.check('language', language, `release/${build}/${LANGUAGE_FILE}`)
    return keepGzipped(join(this.buildDir(build), LANGUAGE_FILE), `${JSON.stringify(language)}\n`)
  }

  hasLanguage(build: string): boolean {
    return existsSync(join(this.buildDir(build), LANGUAGE_FILE))
  }

  /** versions.json, from the build.json files there are. Returns whether it changed. */
  writeVersions(): boolean {
    const builds = this.builds()
    const versions = { formatVersion: FORMAT_VERSION, release: builds.length ? { latest: builds[0], builds } : { builds } }
    this.schemas.check('versions', versions, 'versions.json')
    return keepJson(join(this.root, 'versions.json'), versions)
  }
}
