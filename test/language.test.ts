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

void test("a pack's en_US.lang its contents.json lists and the zip lacks is read from the release's other zip", async () => {
  const dir = tempDir()
  const zip = join(dir, 'linux.zip')
  const other = join(dir, 'windows.zip')
  writeZip(zip, {
    'resource_packs/vanilla/texts/en_US.lang': 'tile.stone.name=Stone\n',
    'resource_packs/chemistry/contents.json': '﻿{ "content": [{ "path": "blocks.json" }, { "path": "texts/en_US.lang" }] }',
    'resource_packs/editor/contents.json': '{ "content": [{ "path": "texts/en_US.lang" }] }',
    'resource_packs/editor/texts/en_US.lang': 'editor.x=X\n',
    'resource_packs/vanilla_1.26.40/contents.json': '{ "content": [{ "path": "textures/x.png" }] }',
  })
  writeZip(other, {
    'resource_packs/vanilla/texts/en_US.lang': 'tile.stone.name=Windows Stone\n',
    'resource_packs/chemistry/texts/en_US.lang': 'tile.element.unknown.name=???\n',
    'resource_packs/editor/texts/en_US.lang': 'editor.x=Windows X\n',
  })
  const asked: string[] = []
  const language = await readLanguage(zip, () => {
    asked.push(other)
    return Promise.resolve(other)
  })
  assert.deepEqual(language.packs, {
    vanilla: { 'tile.stone.name': 'Stone' },
    chemistry: { 'tile.element.unknown.name': '???' },
    editor: { 'editor.x': 'X' },
  })
  assert.deepEqual(asked, [other], 'only the lacking file is read from the other zip, and only then')
  assert.deepEqual(await readLanguage(other, () => Promise.reject(new Error('not asked'))), {
    locale: 'en_US',
    packs: { vanilla: { 'tile.stone.name': 'Windows Stone' }, chemistry: { 'tile.element.unknown.name': '???' }, editor: { 'editor.x': 'Windows X' } },
  })
})

void test('a listed en_US.lang that neither zip has, or a release with no other zip, is refused', async () => {
  const dir = tempDir()
  const zip = join(dir, 'linux.zip')
  const other = join(dir, 'windows.zip')
  writeZip(zip, {
    'resource_packs/vanilla/texts/en_US.lang': 'tile.stone.name=Stone\n',
    'resource_packs/chemistry/contents.json': '{ "content": [{ "path": "texts/en_US.lang" }] }',
  })
  writeZip(other, { 'resource_packs/vanilla/texts/en_US.lang': 'tile.stone.name=Stone\n' })
  await assert.rejects(readLanguage(zip), /no resource_packs\/chemistry\/texts\/en_US\.lang, which its contents\.json lists, and the release has no other zip/)
  await assert.rejects(
    readLanguage(zip, () => Promise.resolve(other)),
    /no resource_packs\/chemistry\/texts\/en_US\.lang, which its contents\.json lists, and .*windows\.zip has none either/,
  )
})

void test('a zip without the vanilla pack is refused', async () => {
  const zip = join(tempDir(), 'server.zip')
  writeZip(zip, { 'resource_packs/chemistry/texts/en_US.lang': 'a=b\n' })
  await assert.rejects(readLanguage(zip), /no resource_packs\/vanilla\/texts\/en_US\.lang/)
})
