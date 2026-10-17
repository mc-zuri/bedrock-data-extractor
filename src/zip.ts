import { createWriteStream, mkdirSync } from 'node:fs'
import { dirname, join } from 'node:path'
import { pipeline } from 'node:stream/promises'
import { open, type Entry, type ZipFile } from 'yauzl'

function openZip(file: string): Promise<ZipFile> {
  return new Promise((resolve, reject) => open(file, { lazyEntries: true, autoClose: true }, (e, zip) => (e ? reject(e) : resolve(zip))))
}

function readStream(zip: ZipFile, entry: Entry): Promise<NodeJS.ReadableStream> {
  return new Promise((resolve, reject) => zip.openReadStream(entry, (e, stream) => (e ? reject(e) : resolve(stream))))
}

/** Each file entry of `zip` (its name with `/`), in the zip's order; `visit` reads it or leaves it. */
async function forEachEntry(zip: string, visit: (name: string, read: () => Promise<NodeJS.ReadableStream>) => Promise<void>): Promise<void> {
  const file = await openZip(zip)
  await new Promise<void>((resolve, reject) => {
    file.on('error', reject)
    file.on('end', resolve)
    file.on('entry', (entry: Entry) => {
      const name = entry.fileName.replace(/\\/g, '/')

      if (name.endsWith('/')) {
        return file.readEntry()
      }

      visit(name, () => readStream(file, entry)).then(() => file.readEntry(), reject)
    })
    file.readEntry()
  })
}

export async function readEntries(zip: string, wanted: (name: string) => boolean): Promise<Map<string, Buffer>> {
  const files = new Map<string, Buffer>()
  await forEachEntry(zip, async (name, read) => {
    if (!wanted(name)) {
      return
    }

    const chunks: Buffer[] = []

    for await (const chunk of await read()) {
      chunks.push(chunk as Buffer)
    }

    files.set(name, Buffer.concat(chunks))
  })
  return files
}

export async function unpack(zip: string, dir: string): Promise<void> {
  await forEachEntry(zip, async (name, read) => {
    const file = join(dir, name)
    mkdirSync(dirname(file), { recursive: true })
    await pipeline(await read(), createWriteStream(file))
  })
}
