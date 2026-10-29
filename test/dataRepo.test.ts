import assert from 'node:assert/strict'
import { existsSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { gunzipSync, gzipSync } from 'node:zlib'
import { DataRepo, type BuildJson } from '../src/dataRepo.ts'
import { gzip } from '../src/files.ts'
import type { Language } from '../src/language.ts'
import { versionOf, zipUrl, type Channel, type Target } from '../src/versions.ts'
import { emptyDataRepo } from './helpers.ts'

const SHA = 'a'.repeat(64)
const RELEASE: Target = { channel: 'release', build: '1.26.52.3' }

function exported(build: string, channel: Channel = 'release'): BuildJson {
  return {
    build,
    version: versionOf(build, channel),
    channel,
    binary: { linux: { url: zipUrl(build, channel, 'linux'), sha256: SHA, size: 1 } },
    data: { language: { file: 'language.json.gz', from: 'linux' } },
  }
}

const LANGUAGE: Language = { locale: 'en_US', packs: { vanilla: { 'tile.stone.name': 'Stone' }, chemistry: { 'item.element_1.name': 'Hydrogen' } } }

void test('gzip has no time and no OS, and reads back', () => {
  const out = gzip('hello')
  assert.equal(out.readUInt32LE(4), 0)
  assert.equal(out[9], 0xff)
  assert.equal(gunzipSync(out).toString('utf8'), 'hello')
})

void test('a file is written only where what it says changed', () => {
  const repo = new DataRepo(emptyDataRepo())
  assert.equal(repo.writeBuild(exported('1.26.52.3')), true)
  assert.equal(repo.writeBuild(exported('1.26.52.3')), false)
  assert.equal(repo.writeLanguage(RELEASE, LANGUAGE), true)
  const file = join(repo.root, 'release', '1.26.52.3', 'language.json.gz')
  // the same text gzipped otherwise (another zlib, another OS byte) is left as it is
  writeFileSync(file, gzipSync(`${JSON.stringify(LANGUAGE)}\n`, { level: 1 }))
  const before = statSync(file).mtimeMs
  assert.equal(repo.writeLanguage(RELEASE, LANGUAGE), false)
  assert.equal(statSync(file).mtimeMs, before)
  assert.equal(repo.writeLanguage(RELEASE, { ...LANGUAGE, packs: { vanilla: { 'tile.stone.name': 'Stone!' } } }), true)
})

void test('what the schemas refuse is not written', () => {
  const repo = new DataRepo(emptyDataRepo())
  assert.throws(() => repo.writeBuild({ ...exported('1.26.52.3'), build: '1.26.52' }), /build\.schema\.json/)
  assert.throws(() => repo.writeBuild({ ...exported('1.26.52.3'), binary: {} }), /build\.schema\.json/)
  assert.throws(() => repo.writeBuild({ ...exported('1.26.52.3'), version: '1.26.52-preview.3' }), /build\.schema\.json/)
  assert.throws(() => repo.writeBuild({ ...exported('1.26.60.30', 'preview'), version: '1.26.60' }), /build\.schema\.json/)
  assert.throws(() => repo.writeLanguage(RELEASE, { locale: 'en_US', packs: { chemistry: {} } }), /language\.schema\.json/)
  assert.deepEqual(repo.builds('release'), [])
})

void test('a preview is kept in preview/<build>', () => {
  const repo = new DataRepo(emptyDataRepo())
  const preview: Target = { channel: 'preview', build: '1.26.60.30' }
  assert.equal(repo.writeBuild(exported('1.26.60.30', 'preview')), true)
  assert.equal(repo.writeLanguage(preview, LANGUAGE), true)
  assert.ok(existsSync(join(repo.root, 'preview', '1.26.60.30', 'language.json.gz')))
  assert.equal(repo.readBuild(preview)?.version, '1.26.60-preview.30')
  assert.equal(repo.readBuild({ channel: 'release', build: '1.26.60.30' }), undefined)
})

void test('versions.json lists the releases and the previews there are, newest first', () => {
  const repo = new DataRepo(emptyDataRepo())

  for (const build of ['1.21.42.01', '1.26.52.3', '1.21.100.6']) {
    repo.writeBuild(exported(build))
  }

  assert.equal(repo.writeVersions(), true)
  assert.deepEqual(JSON.parse(readFileSync(join(repo.root, 'versions.json'), 'utf8')), {
    formatVersion: 1,
    release: { latest: '1.26.52.3', builds: ['1.26.52.3', '1.21.100.6', '1.21.42.01'] },
    preview: { builds: [] },
  })
  assert.equal(repo.writeVersions(), false)

  for (const build of ['1.26.60.29', '1.26.70.21', '1.26.60.30']) {
    repo.writeBuild(exported(build, 'preview'))
  }

  assert.equal(repo.writeVersions(), true)
  assert.deepEqual((JSON.parse(readFileSync(join(repo.root, 'versions.json'), 'utf8')) as { preview: unknown }).preview, {
    latest: '1.26.70.21',
    builds: ['1.26.70.21', '1.26.60.30', '1.26.60.29'],
  })
})
