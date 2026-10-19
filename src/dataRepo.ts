import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { BLOCKS_FILE, ENCHANTMENTS_FILE, ITEMS_FILE, PALETTE_FILE, SHAPES_FILE, type BlockShapes, type BlockTypes, type Enchantments, type Items } from './blocks.ts'
import type { Release } from './extract.ts'
import { STEVE_FILE, type Skin } from './steve.ts'
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
    blocks?: { file: typeof BLOCKS_FILE; from: Platform }
    items?: { file: typeof ITEMS_FILE; from: Platform }
    enchantments?: { file: typeof ENCHANTMENTS_FILE; from: Platform }
  } & Partial<Record<Kind, { file: string; from: Platform }>> & {
      /** recorded from a client of the release's version: no platform's */
      steve?: { file: typeof STEVE_FILE }
    }
}

/** The kinds written from the agent's other files, the packets and the packs, each by its file and its schema */
export const KINDS = {
  biomes: { file: 'biomes.json.gz', schema: 'biomes' },
  effects: { file: 'effects.json.gz', schema: 'effects' },
  enums: { file: 'enums.json.gz', schema: 'enums' },
  tints: { file: 'tints.json.gz', schema: 'tints' },
  recipes: { file: 'recipes.json.gz', schema: 'recipes' },
  entities: { file: 'entities.json.gz', schema: 'entities' },
  commands: { file: 'commands.json.gz', schema: 'commands' },
  attributes: { file: 'attributes.json.gz', schema: 'attributes' },
} as const

export type Kind = keyof typeof KINDS

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

  writeBlocks(build: string, blocks: BlockTypes): boolean {
    this.schemas.check('blocks', blocks, `release/${build}/${BLOCKS_FILE}`)
    return keepGzipped(
      join(this.buildDir(build), BLOCKS_FILE),
      `${JSON.stringify(blocks)}
`,
    )
  }

  writeItems(build: string, items: Items): boolean {
    this.schemas.check('items', items, `release/${build}/${ITEMS_FILE}`)
    return keepGzipped(
      join(this.buildDir(build), ITEMS_FILE),
      `${JSON.stringify(items)}
`,
    )
  }

  writeEnchantments(build: string, enchantments: Enchantments): boolean {
    this.schemas.check('enchantments', enchantments, `release/${build}/${ENCHANTMENTS_FILE}`)
    return keepGzipped(join(this.buildDir(build), ENCHANTMENTS_FILE), `${JSON.stringify(enchantments)}\n`)
  }

  writeKind(build: string, kind: Kind, value: Release['blocks'][Kind]): boolean {
    const { file, schema } = KINDS[kind]
    this.schemas.check(schema, value, `release/${build}/${file}`)
    return keepGzipped(join(this.buildDir(build), file), `${JSON.stringify(value)}\n`)
  }

  /** The release's steve, and its entry in its build.json (which must be there: the release extracted first) */
  writeSteve(build: string, skin: Skin): boolean {
    const json = this.readBuild(build)

    if (!json) {
      throw new Error(`release/${build} is not in bedrock-data: pnpm extract ${build} first`)
    }

    this.schemas.check('steve', skin, `release/${build}/${STEVE_FILE}`)
    const wrote = keepGzipped(join(this.buildDir(build), STEVE_FILE), `${JSON.stringify(skin)}\n`)
    const { steve: _, ...data } = json.data
    return this.writeBuild({ ...json, data: { ...data, steve: { file: STEVE_FILE } } }) || wrote
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
