import assert from 'node:assert/strict'
import { test } from 'node:test'
import { joinAs, ownVersion, type Release } from '../src/client.ts'

const RELEASES: Release[] = [
  { minecraftVersion: '1.26.51', version: 2193 },
  { minecraftVersion: '1.26.45', version: 2169 },
  { minecraftVersion: '1.26.40', version: 2168 },
  { minecraftVersion: '1.26.30', version: 1001 },
]

void test('a release joins as its own version where bedrock-protocol knows it; a preview never does', () => {
  assert.equal(ownVersion({ channel: 'release', build: '1.26.45.1' }, RELEASES), '1.26.45')
  assert.equal(ownVersion({ channel: 'release', build: '1.26.52.3' }, RELEASES), undefined)
  assert.equal(ownVersion({ channel: 'preview', build: '1.26.40.20' }, RELEASES), undefined)
})

void test('a server joins as the newest version of its protocol, else of the newest protocol before it, as its own', () => {
  const t = { channel: 'preview', build: '1.26.60.30' } as const
  assert.deepEqual(joinAs(t, 2193, RELEASES), { version: '1.26.51' })
  assert.deepEqual(joinAs(t, 2207, RELEASES), { version: '1.26.51', protocol: 2207, from: 2193 })
  assert.deepEqual(joinAs(t, 2168, RELEASES), { version: '1.26.40' })
  assert.deepEqual(joinAs(t, 1500, RELEASES), { version: '1.26.30', protocol: 1500, from: 1001 })
  assert.throws(() => joinAs(t, 900, RELEASES), /knows no version of protocol 900 or before \(preview\/1\.26\.60\.30\)/)
})
