import assert from 'node:assert/strict'
import { join } from 'node:path'
import { test } from 'node:test'
import { readLanguage } from '../src/language.ts'
import { tempDir, writeZip } from './helpers.ts'

void test("each pack's en_US.lang, vanilla first, the others by name; nothing else in the zip", async () => {
  const zip = join(tempDir(), 'server.zip')
  writeZip(zip, {
    bedrock_server: 'ELF',
    'resource_packs/chemistry/texts/en_US.lang': 'item.element_1.name=Hydrogen\n',
    'resource_packs/vanilla/texts/de_DE.lang': 'tile.stone.name=Stein\n',
    'resource_packs/editor/texts/en_US.lang': 'editor.x=X\n',
    'resource_packs/vanilla/texts/en_US.lang': '﻿## comment\r\ntile.stone.name=Stone\r\nadvMode.allEntities=@e = all entities\t## note\r\n',
    'resource_packs/vanilla_1.14/manifest.json': '{}',
  })
  const language = await readLanguage(zip)
  assert.deepEqual(language, {
    locale: 'en_US',
    packs: {
      vanilla: { 'tile.stone.name': 'Stone', 'advMode.allEntities': '@e = all entities' },
      chemistry: { 'item.element_1.name': 'Hydrogen' },
      editor: { 'editor.x': 'X' },
    },
  })
  assert.deepEqual(Object.keys(language.packs), ['vanilla', 'chemistry', 'editor'])
})

void test('a zip without the vanilla pack is refused', async () => {
  const zip = join(tempDir(), 'server.zip')
  writeZip(zip, { 'resource_packs/chemistry/texts/en_US.lang': 'a=b\n' })
  await assert.rejects(readLanguage(zip), /no resource_packs\/vanilla\/texts\/en_US\.lang/)
})
