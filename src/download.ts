import { createHash } from 'node:crypto'
import { createReadStream, createWriteStream, existsSync, mkdirSync, renameSync, statSync } from 'node:fs'
import { dirname } from 'node:path'
import { Readable, Transform, Writable } from 'node:stream'
import { pipeline } from 'node:stream/promises'
import type { ReadableStream } from 'node:stream/web'

interface Hashed {
  sha256: string
  size: number
}

interface Zip extends Hashed {
  file: string
}

function counting(): { through: Transform; done: () => Hashed } {
  const hash = createHash('sha256')
  let size = 0
  const through = new Transform({
    transform(chunk: Buffer, _encoding, callback) {
      hash.update(chunk)
      size += chunk.length
      callback(null, chunk)
    },
  })
  return { through, done: () => ({ sha256: hash.digest('hex'), size }) }
}

/** Exactly `url`, never a redirect: a build minecraft.net does not have must not be stood in for. Undefined where it has no such zip. */
async function fetchZip(url: string): Promise<{ body: Readable; length: number } | undefined> {
  const res = await fetch(url, { redirect: 'error' })

  if (res.status === 404) {
    return undefined
  }

  if (!res.ok || !res.body) {
    throw new Error(`${url}: HTTP ${res.status}`)
  }

  const type = res.headers.get('content-type') ?? ''

  if (!type.includes('zip')) {
    throw new Error(`${url}: ${type}, not a zip`)
  }

  return { body: Readable.fromWeb(res.body as ReadableStream), length: Number(res.headers.get('content-length')) }
}

function checkLength(url: string, length: number, got: number): void {
  if (length && got !== length) {
    throw new Error(`${url}: got ${got} of ${length} bytes`)
  }
}

/** The sha256 and size of the zip at `url`, read as it downloads and kept nowhere. Undefined where there is none. */
export async function hashZip(url: string): Promise<Hashed | undefined> {
  const zip = await fetchZip(url)

  if (!zip) {
    return undefined
  }

  const { through, done } = counting()
  await pipeline(zip.body, through, new Writable({ write: (_chunk, _encoding, callback) => callback() }))
  const hashed = done()
  checkLength(url, zip.length, hashed.size)
  return hashed
}

/**
 * The zip at `url` into `file`, undefined where there is none. A `file` already there is a download that finished
 * (it is renamed into place only then), and is hashed instead of downloaded again.
 */
export async function download(url: string, file: string): Promise<Zip | undefined> {
  if (existsSync(file)) {
    const hash = createHash('sha256')

    for await (const chunk of createReadStream(file)) {
      hash.update(chunk as Buffer)
    }

    return { file, sha256: hash.digest('hex'), size: statSync(file).size }
  }

  const zip = await fetchZip(url)

  if (!zip) {
    return undefined
  }

  mkdirSync(dirname(file), { recursive: true })
  const part = `${file}.part`
  const { through, done } = counting()
  await pipeline(zip.body, through, createWriteStream(part))
  checkLength(url, zip.length, statSync(part).size)
  renameSync(part, file)
  return { file, ...done() }
}
