import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { SCHEMAS_DIR } from '../src/config.ts'
import { isTracked } from '../src/track.ts'
import { VersionStore, type VersionJson } from '../src/versionStore.ts'
import { zipUrl, type Channel } from '../src/versions.ts'
import { tempDir } from './helpers.ts'

function version(build: string, channel: Channel, platforms: ('linux' | 'windows')[] = ['linux', 'windows']): VersionJson {
  const [major, minor, patch, number] = build.split('.')
  return {
    build,
    version: channel === 'release' ? `${major}.${minor}.${patch}` : `${major}.${minor}.${patch}-preview.${Number(number)}`,
    channel,
    binary: Object.fromEntries(platforms.map((p) => [p, { url: zipUrl(build, channel, p), sha256: 'b'.repeat(64), size: 9 }])),
  }
}

void test("a build's file is written only where it changed, and as the schema says", () => {
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  assert.equal(store.write(version('1.26.52.3', 'release')), true)
  assert.equal(store.write(version('1.26.52.3', 'release')), false)
  assert.deepEqual(store.read('release', '1.26.52.3'), version('1.26.52.3', 'release'))
  assert.throws(() => store.write({ ...version('1.26.52.3', 'release'), binary: {} }), /version\.schema\.json/)
})

void test('versions.json lists each channel newest first', () => {
  const root = join(tempDir(), 'versions')
  const store = new VersionStore(root, SCHEMAS_DIR)
  for (const build of ['1.21.42.01', '1.26.52.3', '1.21.100.6']) store.write(version(build, 'release'))
  store.write(version('1.26.70.20', 'preview'))
  assert.equal(store.writeIndex(), true)
  assert.deepEqual(JSON.parse(readFileSync(join(root, 'versions.json'), 'utf8')), {
    release: { latest: '1.26.52.3', builds: ['1.26.52.3', '1.21.100.6', '1.21.42.01'] },
    preview: { latest: '1.26.70.20', builds: ['1.26.70.20'] },
  })
  assert.equal(store.writeIndex(), false)
})

void test('a build is tracked once kept with each platform the download links offer', () => {
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  store.write(version('1.21.50.07', 'release', ['windows']))
  assert.ok(isTracked(store, { channel: 'release', build: '1.21.50.07' }))
  assert.ok(!isTracked(store, { channel: 'release', build: '1.21.50.07', platforms: ['linux', 'windows'] }))
  assert.ok(!isTracked(store, { channel: 'preview', build: '1.21.50.07' }))
})
