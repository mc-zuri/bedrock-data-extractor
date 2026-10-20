import { parseLang } from './lang.ts'
import { readEntries } from './zip.ts'

export const LANGUAGE_FILE = 'language.json.gz'

export interface Language {
  locale: 'en_US'
  packs: Record<string, Record<string, string>>
}

const LANG_ENTRY = /^resource_packs\/([^/]+)\/texts\/en_US\.lang$/
const CONTENTS_ENTRY = /^resource_packs\/([^/]+)\/contents\.json$/

function packOrder(a: string, b: string): number {
  return Number(b === 'vanilla') - Number(a === 'vanilla') || (a < b ? -1 : a > b ? 1 : 0)
}

function langOf(pack: string): string {
  return `resource_packs/${pack}/texts/en_US.lang`
}

function listsLang(contents: Buffer): boolean {
  const json = JSON.parse(contents.toString('utf8').replace(/^﻿/, '')) as { content: { path: string }[] }
  return json.content.some((entry) => entry.path === 'texts/en_US.lang')
}

/**
 * Each resource pack's en_US strings in `zip`. A pack whose contents.json lists a `texts/en_US.lang` the zip lacks
 * (1.16.220.02's Linux zip: the chemistry pack's) has it read from `otherZip`, the release's other zip.
 */
export async function readLanguage(zip: string, otherZip?: () => Promise<string>): Promise<Language> {
  const files = new Map<string, Buffer>()
  const listed: string[] = []

  for (const [name, data] of await readEntries(zip, (name) => LANG_ENTRY.test(name) || CONTENTS_ENTRY.test(name))) {
    const lang = LANG_ENTRY.exec(name)

    if (lang) {
      files.set(lang[1], data)
    } else if (listsLang(data)) {
      listed.push(CONTENTS_ENTRY.exec(name)?.[1] ?? '')
    }
  }

  if (!files.has('vanilla')) {
    throw new Error(`${zip} has no resource_packs/vanilla/texts/en_US.lang`)
  }

  const lacking = listed.filter((pack) => !files.has(pack))

  if (lacking.length) {
    if (!otherZip) {
      throw new Error(`${zip} has no ${lacking.map(langOf).join(', ')}, which its contents.json lists, and the release has no other zip`)
    }

    const other = await otherZip()
    const found = await readEntries(other, (name) => lacking.some((pack) => name === langOf(pack)))

    for (const pack of lacking) {
      const data = found.get(langOf(pack))

      if (!data) {
        throw new Error(`${zip} has no ${langOf(pack)}, which its contents.json lists, and ${other} has none either`)
      }

      files.set(pack, data)
    }
  }

  const packs: Record<string, Record<string, string>> = {}

  for (const pack of [...files.keys()].sort(packOrder)) {
    packs[pack] = Object.fromEntries(parseLang(files.get(pack)?.toString('utf8') ?? ''))
  }

  return { locale: 'en_US', packs }
}
