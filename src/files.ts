// Writing a file only where what it says changed, and checking it against a JSON schema first: a run that finds
// nothing new leaves every file as it is, on any machine.
import { Ajv, type ValidateFunction } from 'ajv'
import { existsSync, mkdirSync, readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { gunzipSync, gzipSync } from 'node:zlib'
import { readIfExists, writeAtomic } from './util.ts'

/** The schemas of one folder (`<name>.schema.json`), each compiled once. */
export class Schemas {
  readonly dir: string
  private readonly validators = new Map<string, ValidateFunction>()

  constructor(dir: string) {
    this.dir = dir
  }

  check(name: string, value: unknown, what: string): void {
    let validate = this.validators.get(name)
    if (!validate) {
      const file = join(this.dir, `${name}.schema.json`)
      if (!existsSync(file)) throw new Error(`no ${file}`)
      validate = new Ajv({ allErrors: true }).compile(JSON.parse(readFileSync(file, 'utf8')) as object)
      this.validators.set(name, validate)
    }
    if (!validate(value)) throw new Error(`${what} is not as ${name}.schema.json says: ${JSON.stringify(validate.errors?.slice(0, 3))}`)
  }
}

/** 2-space JSON with a final newline. Returns whether it changed. */
export function keepJson(file: string, value: unknown): boolean {
  const text = `${JSON.stringify(value, null, 2)}\n`
  if (readIfExists(file)?.toString('utf8') === text) return false
  mkdirSync(dirname(file), { recursive: true })
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

/** Returns whether it changed. */
export function keepGzipped(file: string, text: string): boolean {
  const old = readIfExists(file)
  if (old !== undefined && gunzipSync(old).toString('utf8') === text) return false
  mkdirSync(dirname(file), { recursive: true })
  writeAtomic(file, gzip(text))
  return true
}
