// The releases this extractor supports: supported.json, newest first. Only a supported release is exported to
// bedrock-data. A tracked release becomes supported through a pull request: the support workflow adds it (pnpm support),
// exports it and runs the tests, and opens one where that worked, an issue where it did not.
import { existsSync, readFileSync } from 'node:fs'
import { keepJson, Schemas } from './files.ts'
import { compareVersions } from './versions.ts'

export class Supported {
  readonly file: string
  private readonly schemas: Schemas

  /** `file`: supported.json; `schemas`: the folder of supported.schema.json */
  constructor(file: string, schemas: string) {
    this.file = file
    this.schemas = new Schemas(schemas)
  }

  /** newest first */
  builds(): string[] {
    return existsSync(this.file) ? (JSON.parse(readFileSync(this.file, 'utf8')) as string[]) : []
  }

  has(build: string): boolean {
    return this.builds().includes(build)
  }

  /** Returns the builds it added: those not supported already. */
  add(builds: string[]): string[] {
    const known = new Set(this.builds())
    const added = [...new Set(builds)].filter((b) => !known.has(b))
    const all = [...known, ...added].sort((a, b) => compareVersions(b, a))
    this.schemas.check('supported', all, 'supported.json')
    keepJson(this.file, all)
    return added
  }
}
