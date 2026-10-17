import assert from 'node:assert/strict'
import { readFileSync, statSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { gunzipSync, gzipSync } from 'node:zlib'
import { DataRepo, gzip, type BuildJson } from '../src/dataRepo.ts'
import type { Language } from '../src/language.ts'
import { emptyDataRepo } from './helpers.ts'

const SHA = 'a'.repeat(64)

function release(build: string): BuildJson {
  return {
    build,
    version: build.split('.').slice(0, 3).join('.'),
    channel: 'release',
    binary: { linux: { url: `https://www.minecraft.net/bedrockdedicatedserver/bin-linux/bedrock-server-${build}.zip`, sha256: SHA, size: 1 } },
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
  assert.equal(repo.writeBuild(release('1.26.52.3')), true)
  assert.equal(repo.writeBuild(release('1.26.52.3')), false)
  assert.equal(repo.writeLanguage('1.26.52.3', LANGUAGE), true)
  const file = join(repo.root, 'release', '1.26.52.3', 'language.json.gz')
  // the same text gzipped otherwise (another zlib, another OS byte) is left as it is
  writeFileSync(file, gzipSync(`${JSON.stringify(LANGUAGE)}\n`, { level: 1 }))
  const before = statSync(file).mtimeMs
  assert.equal(repo.writeLanguage('1.26.52.3', LANGUAGE), false)
  assert.equal(statSync(file).mtimeMs, before)
  assert.equal(repo.writeLanguage('1.26.52.3', { ...LANGUAGE, packs: { vanilla: { 'tile.stone.name': 'Stone!' } } }), true)
})

void test('what the schemas refuse is not written', () => {
  const repo = new DataRepo(emptyDataRepo())
  assert.throws(() => repo.writeBuild({ ...release('1.26.52.3'), build: '1.26.52' }), /schemas\/build\.schema\.json/)
  assert.throws(() => repo.writeBuild({ ...release('1.26.52.3'), binary: {} }), /schemas\/build\.schema\.json/)
  assert.throws(() => repo.writeLanguage('1.26.52.3', { locale: 'en_US', packs: { chemistry: {} } }), /schemas\/language\.schema\.json/)
  assert.deepEqual(repo.builds('release'), [])
})

void test('versions.json lists the builds there are, newest first', () => {
  const repo = new DataRepo(emptyDataRepo())
  for (const build of ['1.21.42.01', '1.26.52.3', '1.21.100.6']) repo.writeBuild(release(build))
  repo.writeBuild({
    build: '1.26.70.20',
    version: '1.26.70-preview.20',
    channel: 'preview',
    binary: { windows: { url: 'https://www.minecraft.net/bedrockdedicatedserver/bin-win-preview/bedrock-server-1.26.70.20.zip', sha256: SHA, size: 2 } },
  })
  assert.equal(repo.writeVersions(), true)
  assert.deepEqual(JSON.parse(readFileSync(join(repo.root, 'versions.json'), 'utf8')), {
    formatVersion: 1,
    release: { latest: '1.26.52.3', builds: ['1.26.52.3', '1.21.100.6', '1.21.42.01'] },
    preview: { latest: '1.26.70.20', builds: ['1.26.70.20'] },
  })
  assert.equal(repo.writeVersions(), false)
})
