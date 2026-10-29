import assert from 'node:assert/strict'
import { readFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { SCHEMAS_DIR } from '../src/config.ts'
import { isTracked } from '../src/track.ts'
import { VersionStore, type VersionJson } from '../src/versionStore.ts'
import { zipUrl, type Channel, type Target } from '../src/versions.ts'
import { tempDir } from './helpers.ts'

function version(build: string, channel: Channel, platforms: ('linux' | 'windows')[] = ['linux', 'windows']): VersionJson {
  const [major, minor, patch, number] = build.split('.')
  return {
    build,
    version: channel === 'release' ? `${major}.${minor}.${patch}` : `${major}.${minor}.${patch}-preview.${Number(number)}`,
    channel,
    binary: Object.fromEntries(platforms.map((p) => [p, { url: zipUrl(build, channel, p), sha256: 'b'.repeat(64), size: 9, supported: false }])),
  }
}

void test('a build is supported once a platform of it is', () => {
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  const release: Target = { channel: 'release', build: '1.26.51.1' }
  const preview: Target = { channel: 'preview', build: '1.26.60.30' }
  store.write(version('1.26.52.3', 'release'))
  store.write(version(release.build, 'release'))
  store.write(version(preview.build, 'preview'))
  assert.deepEqual(store.supportedBuilds(), [])
  assert.equal(store.setSupported(release, { windows: true }), true)
  assert.equal(store.setSupported(release, { windows: true }), false)
  assert.deepEqual(store.supportedBuilds(), [release])
  store.setSupported(preview, { linux: true })
  assert.deepEqual(store.supportedBuilds(), [release, preview])
  assert.deepEqual(store.read('release', '1.26.51.1')?.binary, {
    linux: { url: zipUrl('1.26.51.1', 'release', 'linux'), sha256: 'b'.repeat(64), size: 9, supported: false },
    windows: { url: zipUrl('1.26.51.1', 'release', 'windows'), sha256: 'b'.repeat(64), size: 9, supported: true },
  })
  assert.equal(store.setSupported(release, { windows: false }), true)
  assert.deepEqual(store.supportedBuilds(), [preview])
})

void test('a build named is of the channel named, else of the one that tracks it', () => {
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  store.write(version('1.26.52.3', 'release'))
  store.write(version('1.26.60.30', 'preview'))
  assert.deepEqual(store.target('1.26.60.30'), { channel: 'preview', build: '1.26.60.30' })
  assert.deepEqual(store.target('1.26.52.3', 'release'), { channel: 'release', build: '1.26.52.3' })
  assert.throws(() => store.target('1.26.52.3', 'preview'), /preview\/1\.26\.52\.3 is not tracked: pnpm track 1\.26\.52\.3 --channel=preview/)
  assert.throws(() => store.target('1.26.53.1'), /1\.26\.53\.1 is not tracked/)
  store.write(version('1.26.52.3', 'preview'))
  assert.throws(() => store.target('1.26.52.3'), /tracked as a release and as a preview/)
})

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

  for (const build of ['1.21.42.01', '1.26.52.3', '1.21.100.6']) {
    store.write(version(build, 'release'))
  }

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
