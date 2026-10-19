import assert from 'node:assert/strict'
import { mkdirSync, readFileSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { gunzipSync } from 'node:zlib'
import { checkSteve } from '../src/checks.ts'
import { DataRepo } from '../src/dataRepo.ts'
import { extract } from '../src/extract.ts'
import { importedSkin, normalizedSkin, type Skin } from '../src/steve.ts'
import { cachedRelease, emptyDataRepo, fakeBlocks, tempDir, VANILLA } from './helpers.ts'

const BUILD = '1.26.52.3'

function base64(text: string): string {
  return Buffer.from(text, 'utf8').toString('base64')
}

/** A login's skin as a client sends it, its account's persona id in it */
function login(persona = 'persona-437vsr5lh19kek5q-3'): Skin {
  return {
    SkinId: `5eb65f73-af11-448e-82aa-1b7b165316ad.${persona}`,
    PlayFabId: '437vsr5lh19kek5q',
    AnimatedImageData: [],
    ArmSize: 'wide',
    CapeData: '',
    CapeId: '',
    CapeImageHeight: 0,
    CapeImageWidth: 0,
    CapeOnClassicSkin: false,
    PersonaPieces: [],
    PersonaSkin: true,
    PieceTintColors: [],
    PremiumSkin: false,
    SkinAnimationData: '',
    SkinColor: '#0',
    SkinData: Buffer.alloc(64 * 64 * 4).toString('base64'),
    SkinGeometryData: base64(`{"geometry": "geometry.${persona.replace('-', '_')}"}`),
    SkinImageHeight: 64,
    SkinImageWidth: 64,
    SkinResourcePatch: base64(`{"geometry": {"default": "geometry.${persona}"}}`),
  }
}

void test("a skin: the login's skin fields in order, every persona id the fixed one", () => {
  const skin = normalizedSkin(login())
  assert.deepEqual(Object.keys(skin).slice(0, 3), ['AnimatedImageData', 'ArmSize', 'CapeData'])
  assert.ok(!('PlayFabId' in skin))
  assert.equal(skin.SkinId, '5eb65f73-af11-448e-82aa-1b7b165316ad.persona-123456789012345-0')
  assert.equal(Buffer.from(skin.SkinResourcePatch as string, 'base64').toString(), '{"geometry": {"default": "geometry.persona-123456789012345-0"}}')
  assert.equal(Buffer.from(skin.SkinGeometryData as string, 'base64').toString(), '{"geometry": "geometry.persona_123456789012345-0"}')
  assert.deepEqual(normalizedSkin(login('persona-3891382d5e3f67c4-0')), skin, 'captures of the same skin by two accounts are equal')
  checkSteve(skin)
})

void test('a skin with an account in it, or pixels not its size, fails', () => {
  assert.throws(() => checkSteve(login()), /persona id of an account/)
  assert.throws(() => checkSteve({ ...normalizedSkin(login()), SkinImageWidth: 128 }), /not 128 x 64 RGBA/)
})

void test("a release's steve goes into bedrock-data with its entry in build.json, which extract keeps", async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  const skin = normalizedSkin(login())
  assert.throws(() => repo.writeSteve(BUILD, skin), /pnpm extract 1\.26\.52\.3 first/)
  await extract(repo, store, BUILD, false, cache, fakeBlocks())
  assert.ok(repo.writeSteve(BUILD, skin))
  assert.ok(!repo.writeSteve(BUILD, skin), 'the same skin again changes nothing')
  assert.deepEqual(JSON.parse(gunzipSync(readFileSync(join(repo.buildDir(BUILD), 'steve.json.gz'))).toString('utf8')), skin)
  const again = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  await extract(repo, again.store, BUILD, true, again.cache, fakeBlocks())
  assert.deepEqual(repo.readBuild(BUILD)?.data.steve, { file: 'steve.json.gz' })
  assert.equal(Object.keys(repo.readBuild(BUILD)?.data ?? {}).at(-1), 'steve')
})

void test('an earlier capture is imported from <dir>/<build>/steve.json, normalized', () => {
  const dir = tempDir()
  mkdirSync(join(dir, BUILD))
  writeFileSync(join(dir, BUILD, 'steve.json'), JSON.stringify(login()))
  assert.deepEqual(importedSkin(dir, BUILD), normalizedSkin(login()))
  assert.equal(importedSkin(dir, '1.26.51.1'), undefined)
})
