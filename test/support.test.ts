import assert from 'node:assert/strict'
import { existsSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { checkLanguage } from '../src/checks.ts'
import { support } from '../src/support.ts'
import { cachedRelease, VANILLA } from './helpers.ts'

const BUILD = '1.26.52.3'

void test('each platform whose zip reads and passes the checks is marked supported; the others are told', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: 'tile.stone.name=Stone\n' })
  const result = await support(store, BUILD, ['linux', 'windows'], cache)
  assert.deepEqual(result.supported, ['linux'])
  assert.deepEqual(
    result.failed.map((f) => f.platform),
    ['windows'],
  )
  assert.match(result.failed[0].error, /the vanilla pack has 1 strings/)
  const json = store.read('release', BUILD)
  assert.equal(json?.binary.linux?.supported, true)
  assert.equal(json?.binary.windows?.supported, false)
  assert.ok(!existsSync(join(cache, 'release', BUILD)), 'its zips are deleted')
})

void test('only the platforms asked for are tried', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: VANILLA })
  const result = await support(store, BUILD, ['windows'], cache)
  assert.deepEqual(result, { supported: ['windows'], failed: [] })
  assert.equal(store.read('release', BUILD)?.binary.linux?.supported, false)
  assert.deepEqual(store.supportedBuilds(), [BUILD])
})

void test('a platform supported already stays so where it fails now', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'], ['linux'])
  const result = await support(store, BUILD, ['linux'], cache)
  assert.match(result.failed[0].error, /is not the zip tracked/)
  assert.equal(store.read('release', BUILD)?.binary.linux?.supported, true)
})

void test("a language is refused with too few vanilla strings, or keys minecraft-data's schema refuses", () => {
  const vanilla = Object.fromEntries(Array.from({ length: 6000 }, (_, i) => [`tile.block_${i}.name`, `Block ${i}`]))
  checkLanguage({ locale: 'en_US', packs: { vanilla } })
  assert.throws(() => checkLanguage({ locale: 'en_US', packs: { vanilla: { 'tile.stone.name': 'Stone' } } }), /1 strings/)
  assert.throws(() => checkLanguage({ locale: 'en_US', packs: { vanilla, editor: { 'bad key': 'x' } } }), /editor pack has keys .* bad key/)
})
