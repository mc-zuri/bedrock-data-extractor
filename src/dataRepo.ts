import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { PALETTE_FILE, SHAPES_FILE, type BlockShapes } from './blocks.ts'
import { keepGzipped, keepJson, Schemas } from './files.ts'
import { LANGUAGE_FILE, type Language } from './language.ts'
import { compareVersions, isBuild, type Platform } from './versions.ts'

const FORMAT_VERSION = 1

export interface BuildJson {
  build: string
  version: string
  channel: 'release'
  binary: Partial<Record<Platform, { url: string; sha256: string; size: number }>>
  data: {
    language?: { file: typeof LANGUAGE_FILE; from: Platform }
    blockPalette?: { file: typeof PALETTE_FILE; from: Platform }
    blockShapes?: { file: typeof SHAPES_FILE; from: Platform }
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

  builds(): string[] {
    const dir = join(this.root, 'release')

    if (!existsSync(dir)) {
      return []
    }

    return readdirSync(dir)
      .filter((build) => isBuild(build) && existsSync(join(dir, build, 'build.json')))
      .sort((a, b) => compareVersions(b, a))
  }

  writeBuild(json: BuildJson): boolean {
    this.schemas.check('build', json, `release/${json.build}/build.json`)
    return keepJson(join(this.buildDir(json.build), 'build.json'), json)
  }

  writeLanguage(build: string, language: Language): boolean {
    this.schemas.check('language', language, `release/${build}/${LANGUAGE_FILE}`)
    return keepGzipped(join(this.buildDir(build), LANGUAGE_FILE), `${JSON.stringify(language)}\n`)
  }

  writePalette(build: string, palette: Buffer): boolean {
    return keepGzipped(join(this.buildDir(build), PALETTE_FILE), palette)
  }

  writeShapes(build: string, shapes: BlockShapes): boolean {
    this.schemas.check('block_shapes', shapes, `release/${build}/${SHAPES_FILE}`)
    return keepGzipped(
      join(this.buildDir(build), SHAPES_FILE),
      `${JSON.stringify(shapes)}
`,
    )
  }

  has(build: string, file: string): boolean {
    return existsSync(join(this.buildDir(build), file))
  }

  writeVersions(): boolean {
    const builds = this.builds()
    const versions = { formatVersion: FORMAT_VERSION, release: builds.length ? { latest: builds[0], builds } : { builds } }
    this.schemas.check('versions', versions, 'versions.json')
    return keepJson(join(this.root, 'versions.json'), versions)
  }
}
