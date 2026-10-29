import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { BLOCKS_FILE, ENCHANTMENTS_FILE, ITEMS_FILE, PALETTE_FILE, SHAPES_FILE, type BlockShapes, type BlockTypes, type Enchantments, type Items } from './blocks.ts'
import type { Release } from './extract.ts'
import { STEVE_FILE, type Skin } from './steve.ts'
import { keepGzipped, keepJson, Schemas } from './files.ts'
import { LANGUAGE_FILE, type Language } from './language.ts'
import { CHANNELS, compareVersions, isBuild, targetName, type Channel, type Platform, type Target } from './versions.ts'

const FORMAT_VERSION = 1

export interface BuildJson {
  build: string
  version: string
  channel: Channel
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

  buildDir(t: Target): string {
    return join(this.root, t.channel, t.build)
  }

  readBuild(t: Target): BuildJson | undefined {
    const file = join(this.buildDir(t), 'build.json')
    return existsSync(file) ? (JSON.parse(readFileSync(file, 'utf8')) as BuildJson) : undefined
  }

  builds(channel: Channel): string[] {
    const dir = join(this.root, channel)

    if (!existsSync(dir)) {
      return []
    }

    return readdirSync(dir)
      .filter((build) => isBuild(build) && existsSync(join(dir, build, 'build.json')))
      .sort((a, b) => compareVersions(b, a))
  }

  writeBuild(json: BuildJson): boolean {
    this.schemas.check('build', json, `${targetName(json)}/build.json`)
    return keepJson(join(this.buildDir(json), 'build.json'), json)
  }

  writeLanguage(t: Target, language: Language): boolean {
    this.schemas.check('language', language, `${targetName(t)}/${LANGUAGE_FILE}`)
    return keepGzipped(join(this.buildDir(t), LANGUAGE_FILE), `${JSON.stringify(language)}\n`)
  }

  writePalette(t: Target, palette: Buffer): boolean {
    return keepGzipped(join(this.buildDir(t), PALETTE_FILE), palette)
  }

  writeShapes(t: Target, shapes: BlockShapes): boolean {
    this.schemas.check('block_shapes', shapes, `${targetName(t)}/${SHAPES_FILE}`)
    return keepGzipped(
      join(this.buildDir(t), SHAPES_FILE),
      `${JSON.stringify(shapes)}
`,
    )
  }

  writeBlocks(t: Target, blocks: BlockTypes): boolean {
    this.schemas.check('blocks', blocks, `${targetName(t)}/${BLOCKS_FILE}`)
    return keepGzipped(
      join(this.buildDir(t), BLOCKS_FILE),
      `${JSON.stringify(blocks)}
`,
    )
  }

  writeItems(t: Target, items: Items): boolean {
    this.schemas.check('items', items, `${targetName(t)}/${ITEMS_FILE}`)
    return keepGzipped(
      join(this.buildDir(t), ITEMS_FILE),
      `${JSON.stringify(items)}
`,
    )
  }

  writeEnchantments(t: Target, enchantments: Enchantments): boolean {
    this.schemas.check('enchantments', enchantments, `${targetName(t)}/${ENCHANTMENTS_FILE}`)
    return keepGzipped(join(this.buildDir(t), ENCHANTMENTS_FILE), `${JSON.stringify(enchantments)}\n`)
  }

  writeKind(t: Target, kind: Kind, value: Release['blocks'][Kind]): boolean {
    const { file, schema } = KINDS[kind]
    this.schemas.check(schema, value, `${targetName(t)}/${file}`)
    return keepGzipped(join(this.buildDir(t), file), `${JSON.stringify(value)}\n`)
  }

  /** The build's steve, and its entry in its build.json (which must be there: the build extracted first) */
  writeSteve(t: Target, skin: Skin): boolean {
    const json = this.readBuild(t)

    if (!json) {
      throw new Error(`${targetName(t)} is not in bedrock-data: pnpm extract ${t.build} first`)
    }

    this.schemas.check('steve', skin, `${targetName(t)}/${STEVE_FILE}`)
    const wrote = keepGzipped(join(this.buildDir(t), STEVE_FILE), `${JSON.stringify(skin)}\n`)
    const { steve: _, ...data } = json.data
    return this.writeBuild({ ...json, data: { ...data, steve: { file: STEVE_FILE } } }) || wrote
  }

  has(t: Target, file: string): boolean {
    return existsSync(join(this.buildDir(t), file))
  }

  writeVersions(): boolean {
    const versions: Record<string, unknown> = { formatVersion: FORMAT_VERSION }

    for (const channel of CHANNELS) {
      const builds = this.builds(channel)
      versions[channel] = builds.length ? { latest: builds[0], builds } : { builds }
    }

    this.schemas.check('versions', versions, 'versions.json')
    return keepJson(join(this.root, 'versions.json'), versions)
  }
}
