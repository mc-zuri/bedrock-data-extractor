// What extracted data must hold before it is written: what the schema cannot say. A build that fails these is not
// supported until the extraction is fixed for it (the support workflow opens an issue).
import type { Language } from './language.ts'

// minecraft-data's language_schema.json: the keys a language.json may have
const LANGUAGE_KEY = /^[a-zA-Z.:0-9_-]+$/
// every release since 1.16.201 has over 8000; a few hundred means the file was cut short or misread
const MIN_VANILLA_STRINGS = 5000

export function checkLanguage(language: Language): void {
  const vanilla = Object.keys(language.packs.vanilla ?? {})
  if (vanilla.length < MIN_VANILLA_STRINGS) throw new Error(`the vanilla pack has ${vanilla.length} strings, fewer than the ${MIN_VANILLA_STRINGS} every release has`)
  for (const [pack, strings] of Object.entries(language.packs)) {
    const bad = Object.keys(strings).filter((key) => !LANGUAGE_KEY.test(key))
    if (bad.length) throw new Error(`the ${pack} pack has keys minecraft-data's language schema refuses: ${bad.slice(0, 5).join(', ')}`)
  }
}
