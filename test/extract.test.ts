import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { gunzipSync } from 'node:zlib'
import { SCHEMAS_DIR } from '../src/config.ts'
import { DataRepo } from '../src/dataRepo.ts'
import { extract, isExtracted } from '../src/extract.ts'
import { VersionStore } from '../src/versionStore.ts'
import { zipUrl } from '../src/versions.ts'
import { emptyDataRepo, tempDir, writeZip } from './helpers.ts'

const BUILD = '1.26.52.3'

/** A tracked release whose linux zip is already in the cache, as a download that finished leaves it: nothing is fetched. */
function setup(trackedSha?: string): { repo: DataRepo; store: VersionStore; cache: string } {
  const cache = tempDir()
  const zip = join(cache, 'release', BUILD, 'linux.zip')
  mkdirSync(join(zip, '..'), { recursive: true })
  writeZip(zip, { 'resource_packs/vanilla/texts/en_US.lang': 'tile.stone.name=Stone\n', 'resource_packs/chemistry/texts/en_US.lang': 'item.element_1.name=Hydrogen\n' })
  const sha256 = trackedSha ?? createHash('sha256').update(readFileSync(zip)).digest('hex')
  const store = new VersionStore(join(tempDir(), 'versions'), SCHEMAS_DIR)
  store.write({
    build: BUILD,
    version: '1.26.52',
    channel: 'release',
    binary: {
      linux: { url: zipUrl(BUILD, 'release', 'linux'), sha256, size: statSync(zip).size },
      windows: { url: zipUrl(BUILD, 'release', 'windows'), sha256: 'c'.repeat(64), size: 5 },
    },
  })
  return { repo: new DataRepo(emptyDataRepo()), store, cache }
}

void test("a release's data is read out of its linux zip, and its version file kept with it", async () => {
  const { repo, store, cache } = setup()
  assert.equal(await extract(repo, store, BUILD, false, cache), 'build.json, language.json.gz written (from linux)')
  const language = JSON.parse(gunzipSync(readFileSync(join(repo.buildDir(BUILD), 'language.json.gz'))).toString('utf8')) as unknown
  assert.deepEqual(language, { locale: 'en_US', packs: { vanilla: { 'tile.stone.name': 'Stone' }, chemistry: { 'item.element_1.name': 'Hydrogen' } } })
  assert.deepEqual(repo.readBuild(BUILD), { ...store.read('release', BUILD), data: { language: { file: 'language.json.gz', from: 'linux' } } })
  assert.ok(!existsSync(join(cache, 'release', BUILD)), 'its zip is deleted once it is done')
  const version = store.read('release', BUILD)
  assert.ok(version && isExtracted(repo, version))
  assert.equal(await extract(repo, store, BUILD, false, cache), undefined)
})

void test('a release whose zips versions/ has otherwise now is extracted again', async () => {
  const { repo, store, cache } = setup()
  await extract(repo, store, BUILD, false, cache)
  const version = store.read('release', BUILD)
  assert.ok(version)
  assert.ok(!isExtracted(repo, { ...version, binary: { linux: version.binary.linux } }))
})

void test('a zip that is not the one tracked is refused, and nothing is written', async () => {
  const { repo, store, cache } = setup('d'.repeat(64))
  await assert.rejects(extract(repo, store, BUILD, false, cache), /is not the zip tracked/)
  assert.equal(repo.readBuild(BUILD), undefined)
})

void test('a build not tracked is refused', async () => {
  const { repo, store, cache } = setup()
  await assert.rejects(extract(repo, store, '1.26.53.1', false, cache), /not tracked: pnpm track 1\.26\.53\.1/)
})
