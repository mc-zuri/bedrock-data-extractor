import assert from 'node:assert/strict'
import { existsSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { checkLanguage } from '../src/checks.ts'
import { support } from '../src/support.ts'
import type { Target } from '../src/versions.ts'
import { cachedRelease, fakeBlocks, VANILLA } from './helpers.ts'

const BUILD = '1.26.52.3'
const RELEASE: Target = { channel: 'release', build: BUILD }

void test('each platform whose zip reads and passes the checks is marked supported; the others are told', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: 'tile.stone.name=Stone\n' })
  const result = await support(store, RELEASE, ['linux', 'windows'], cache, fakeBlocks())
  assert.deepEqual(result.supported, ['linux'])
  assert.deepEqual(
    result.failed.map((f) => f.platform),
    ['windows'],
  )
  assert.match(result.failed[0].error, /the vanilla pack has 1 strings/)
  const json = store.read('release', BUILD)
  assert.equal(json?.binary.linux?.supported, true)
  assert.equal(json?.binary.windows?.supported, false)
  assert.ok(existsSync(join(cache, 'release', BUILD, 'windows.zip')), 'a zip that failed is kept for the next try')
})

void test('a release whose platforms all pass has its zips deleted', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: VANILLA })
  assert.deepEqual(await support(store, RELEASE, ['linux', 'windows'], cache, fakeBlocks()), { supported: ['linux', 'windows'], failed: [] })
  assert.ok(!existsSync(join(cache, 'release', BUILD)))
})

void test('only the platforms asked for are tried', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: VANILLA })
  const blocks = fakeBlocks()
  const result = await support(store, RELEASE, ['windows'], cache, blocks)
  assert.deepEqual(result, { supported: ['windows'], failed: [] })
  assert.deepEqual(blocks.calls, [{ build: BUILD, platform: 'windows' }])
  assert.equal(store.read('release', BUILD)?.binary.linux?.supported, false)
  assert.deepEqual(store.supportedBuilds(), [RELEASE])
})

void test('a platform supported already is marked unsupported where it fails now', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  const result = await support(store, RELEASE, ['linux'], cache, fakeBlocks(new Error('the agent: no visual shape for minecraft:stone #0')))
  assert.match(result.failed[0].error, /no visual shape/)
  assert.equal(store.read('release', BUILD)?.binary.linux?.supported, false)
  assert.deepEqual(store.supportedBuilds(), [])
})

void test('a preview is supported as a release is', async () => {
  const preview: Target = { channel: 'preview', build: '1.26.60.30' }
  const { store, cache } = cachedRelease(preview.build, { linux: VANILLA, windows: VANILLA }, [], [], 'preview')
  assert.deepEqual(await support(store, preview, ['linux'], cache, fakeBlocks()), { supported: ['linux'], failed: [] })
  assert.equal(store.read('preview', preview.build)?.binary.linux?.supported, true)
  assert.deepEqual(store.supportedBuilds(), [preview])
  assert.ok(!existsSync(join(cache, 'preview', preview.build)))
})

void test('a language is refused with too few vanilla strings, or keys that are not language keys', () => {
  const vanilla = Object.fromEntries(Array.from({ length: 6000 }, (_, i) => [`tile.block_${i}.name`, `Block ${i}`]))
  checkLanguage({ locale: 'en_US', packs: { vanilla } })
  assert.throws(() => checkLanguage({ locale: 'en_US', packs: { vanilla: { 'tile.stone.name': 'Stone' } } }), /1 strings/)
  assert.throws(() => checkLanguage({ locale: 'en_US', packs: { vanilla, editor: { 'bad key': 'x' } } }), /editor pack has keys .* bad key/)
})
