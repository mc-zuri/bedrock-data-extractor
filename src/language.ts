// A build's en_US strings: each resource pack's texts/en_US.lang in the server zip, parsed (lang.ts), the vanilla pack
// first. Read out of the zip as it is: nothing is unpacked.
import { open, type Entry, type ZipFile } from 'yauzl'
import { parseLang } from './lang.ts'

export const LANGUAGE_FILE = 'language.json.gz'

export interface Language {
  locale: 'en_US'
  packs: Record<string, Record<string, string>>
}

const LANG_ENTRY = /^resource_packs\/([^/]+)\/texts\/en_US\.lang$/

/** vanilla, then the others by name (code unit order: the same on every machine, whatever its locale) */
function packOrder(a: string, b: string): number {
  return Number(b === 'vanilla') - Number(a === 'vanilla') || (a < b ? -1 : a > b ? 1 : 0)
}

function openZip(file: string): Promise<ZipFile> {
  return new Promise((resolve, reject) => open(file, { lazyEntries: true, autoClose: true }, (e, zip) => (e ? reject(e) : resolve(zip))))
}

function readEntry(zip: ZipFile, entry: Entry): Promise<Buffer> {
  return new Promise((resolve, reject) =>
    zip.openReadStream(entry, (e, stream) => {
      if (e) return reject(e)
      const chunks: Buffer[] = []
      stream.on('data', (chunk: Buffer) => chunks.push(chunk))
      stream.on('error', reject)
      stream.on('end', () => resolve(Buffer.concat(chunks)))
    }),
  )
}

/** Each pack's en_US.lang in `zip`, by pack, as the zip has it. */
export async function langFiles(zip: string): Promise<Map<string, Buffer>> {
  const file = await openZip(zip)
  const files = new Map<string, Buffer>()
  await new Promise<void>((resolve, reject) => {
    file.on('error', reject)
    file.on('end', resolve)
    file.on('entry', (entry: Entry) => {
      const pack = LANG_ENTRY.exec(entry.fileName.replace(/\\/g, '/'))?.[1]
      if (pack === undefined) return file.readEntry()
      readEntry(file, entry).then((data) => {
        files.set(pack, data)
        file.readEntry()
      }, reject)
    })
    file.readEntry()
  })
  return files
}

export async function readLanguage(zip: string): Promise<Language> {
  const files = await langFiles(zip)
  if (!files.has('vanilla')) throw new Error(`${zip} has no resource_packs/vanilla/texts/en_US.lang`)
  const packs: Record<string, Record<string, string>> = {}
  for (const pack of [...files.keys()].sort(packOrder)) packs[pack] = Object.fromEntries(parseLang(files.get(pack)?.toString('utf8') ?? ''))
  return { locale: 'en_US', packs }
}
