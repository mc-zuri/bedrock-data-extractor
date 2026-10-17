import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { checkLanguage } from '../src/checks.ts'
import { SCHEMAS_DIR } from '../src/config.ts'
import { Supported } from '../src/supported.ts'
import { tempDir } from './helpers.ts'

void test('a release is added once, newest first', () => {
  const file = join(tempDir(), 'supported.json')
  const supported = new Supported(file, SCHEMAS_DIR)
  assert.deepEqual(supported.add(['1.21.42.01', '1.26.52.3']), ['1.21.42.01', '1.26.52.3'])
  assert.deepEqual(supported.add(['1.26.52.3', '1.21.100.6']), ['1.21.100.6'])
  assert.deepEqual(JSON.parse(readFileSync(file, 'utf8')), ['1.26.52.3', '1.21.100.6', '1.21.42.01'])
  assert.ok(supported.has('1.21.100.6'))
  assert.throws(() => supported.add(['1.26.52']), /supported\.schema\.json/)
})

void test("a language is refused with too few vanilla strings, or keys minecraft-data's schema refuses", () => {
  const vanilla = Object.fromEntries(Array.from({ length: 6000 }, (_, i) => [`tile.block_${i}.name`, `Block ${i}`]))
  checkLanguage({ locale: 'en_US', packs: { vanilla } })
  assert.throws(() => checkLanguage({ locale: 'en_US', packs: { vanilla: { 'tile.stone.name': 'Stone' } } }), /1 strings/)
  assert.throws(() => checkLanguage({ locale: 'en_US', packs: { vanilla, editor: { 'bad key': 'x' } } }), /editor pack has keys .* bad key/)
})
