import { parseLang } from './lang.ts'
import { readEntries } from './zip.ts'

export const LANGUAGE_FILE = 'language.json.gz'

export interface Language {
  locale: 'en_US'
  packs: Record<string, Record<string, string>>
}

const LANG_ENTRY = /^resource_packs\/([^/]+)\/texts\/en_US\.lang$/

function packOrder(a: string, b: string): number {
  return Number(b === 'vanilla') - Number(a === 'vanilla') || (a < b ? -1 : a > b ? 1 : 0)
}

export async function readLanguage(zip: string): Promise<Language> {
  const files = new Map<string, Buffer>()

  for (const [name, data] of await readEntries(zip, (name) => LANG_ENTRY.test(name))) {
    files.set(LANG_ENTRY.exec(name)?.[1] ?? '', data)
  }

  if (!files.has('vanilla')) {
    throw new Error(`${zip} has no resource_packs/vanilla/texts/en_US.lang`)
  }

  const packs: Record<string, Record<string, string>> = {}

  for (const pack of [...files.keys()].sort(packOrder)) {
    packs[pack] = Object.fromEntries(parseLang(files.get(pack)?.toString('utf8') ?? ''))
  }

  return { locale: 'en_US', packs }
}
