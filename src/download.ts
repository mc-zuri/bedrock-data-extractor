import { createHash } from 'node:crypto'
import { createReadStream, createWriteStream, existsSync, mkdirSync, renameSync, statSync } from 'node:fs'
import { dirname } from 'node:path'
import { Readable, Transform } from 'node:stream'
import { pipeline } from 'node:stream/promises'
import type { ReadableStream } from 'node:stream/web'

export interface Zip {
  file: string
  sha256: string
  size: number
}

function counting(): { through: Transform; done: () => { sha256: string; size: number } } {
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

/**
 * Exactly `url` into `file`, never a redirect: a build minecraft.net does not have must not be stood in for. Undefined
 * where it has no such zip. A `file` already there is a download that finished (it is renamed into place only then),
 * and is hashed instead of downloaded again.
 */
export async function download(url: string, file: string): Promise<Zip | undefined> {
  if (existsSync(file)) {
    const hash = createHash('sha256')
    for await (const chunk of createReadStream(file)) hash.update(chunk as Buffer)
    return { file, sha256: hash.digest('hex'), size: statSync(file).size }
  }
  const res = await fetch(url, { redirect: 'error' })
  if (res.status === 404) return undefined
  if (!res.ok || !res.body) throw new Error(`${url}: HTTP ${res.status}`)
  const type = res.headers.get('content-type') ?? ''
  if (!type.includes('zip')) throw new Error(`${url}: ${type}, not a zip`)
  mkdirSync(dirname(file), { recursive: true })
  const part = `${file}.part`
  const { through, done } = counting()
  await pipeline(Readable.fromWeb(res.body as ReadableStream), through, createWriteStream(part))
  const length = Number(res.headers.get('content-length'))
  if (length && statSync(part).size !== length) throw new Error(`${url}: got ${statSync(part).size} of ${length} bytes`)
  renameSync(part, file)
  return { file, ...done() }
}
