import { Ajv, type ValidateFunction } from 'ajv'
import { existsSync, mkdirSync, readFileSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { gunzipSync, gzipSync } from 'node:zlib'
import { readIfExists, writeAtomic } from './util.ts'

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

      if (!existsSync(file)) {
        throw new Error(`no ${file}`)
      }

      validate = new Ajv({ allErrors: true }).compile(JSON.parse(readFileSync(file, 'utf8')) as object)
      this.validators.set(name, validate)
    }

    if (!validate(value)) {
      throw new Error(`${what} is not as ${name}.schema.json says: ${JSON.stringify(validate.errors?.slice(0, 3))}`)
    }
  }
}

export function keepJson(file: string, value: unknown): boolean {
  const text = `${JSON.stringify(value, null, 2)}\n`

  if (readIfExists(file)?.toString('utf8') === text) {
    return false
  }

  mkdirSync(dirname(file), { recursive: true })
  writeAtomic(file, text)
  return true
}

export function gzip(data: string | Buffer): Buffer {
  const out = gzipSync(data, { level: 9 })
  out[9] = 0xff
  return out
}

export function keepGzipped(file: string, data: string | Buffer): boolean {
  const old = readIfExists(file)

  if (old !== undefined && gunzipSync(old).equals(Buffer.from(data))) {
    return false
  }

  mkdirSync(dirname(file), { recursive: true })
  writeAtomic(file, gzip(data))
  return true
}
