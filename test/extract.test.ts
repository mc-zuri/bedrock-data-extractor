import assert from 'node:assert/strict'
import { createHash } from 'node:crypto'
import { existsSync, mkdirSync, readFileSync, statSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { gunzipSync } from 'node:zlib'
import { SCHEMAS_DIR } from '../src/config.ts'
import { DataRepo } from '../src/dataRepo.ts'
import { extract, isExtracted } from '../src/extract.ts'
import type { Language } from '../src/language.ts'
import { VersionStore } from '../src/versionStore.ts'
import { zipUrl } from '../src/versions.ts'
import { emptyDataRepo, tempDir, writeZip } from './helpers.ts'

const BUILD = '1.26.52.3'
// as many strings as a release's vanilla pack has (checks.ts)
const VANILLA = Array.from({ length: 6000 }, (_, i) => `tile.block_${i}.name=Block ${i}`).join('\n')

/** A tracked release whose linux zip is already in the cache, as a download that finished leaves it: nothing is fetched. */
function setup(vanilla = VANILLA, trackedSha?: string): { repo: DataRepo; store: VersionStore; cache: string } {
  const cache = tempDir()
  const zip = join(cache, 'release', BUILD, 'linux.zip')
  mkdirSync(join(zip, '..'), { recursive: true })
  writeZip(zip, { 'resource_packs/vanilla/texts/en_US.lang': vanilla, 'resource_packs/chemistry/texts/en_US.lang': 'item.element_1.name=Hydrogen\n' })
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
  assert.equal(await extract(repo, store, BUILD, false, cache), 'build.json, language.json.gz written (from linux: 6000 vanilla strings)')
  const language = JSON.parse(gunzipSync(readFileSync(join(repo.buildDir(BUILD), 'language.json.gz'))).toString('utf8')) as Language
  assert.equal(Object.keys(language.packs.vanilla).length, 6000)
  assert.deepEqual(language.packs.chemistry, { 'item.element_1.name': 'Hydrogen' })
  assert.deepEqual(repo.readBuild(BUILD), { ...store.read('release', BUILD), data: { language: { file: 'language.json.gz', from: 'linux' } } })
  assert.ok(!existsSync(join(cache, 'release', BUILD)), 'its zip is deleted once it is done')
})

void test('a release bedrock-data has the data of is left as it is, unless forced', async () => {
  const { repo, store, cache } = setup()
  await extract(repo, store, BUILD, false, cache)
  assert.ok(isExtracted(repo, BUILD))
  assert.equal(await extract(repo, store, BUILD, false, cache), undefined)
})

void test('data that fails the checks is not written', async () => {
  const { repo, store, cache } = setup('tile.stone.name=Stone\n')
  await assert.rejects(extract(repo, store, BUILD, false, cache), /the vanilla pack has 1 strings/)
  assert.equal(repo.readBuild(BUILD), undefined)
})

void test('a zip that is not the one tracked is refused, and nothing is written', async () => {
  const { repo, store, cache } = setup(VANILLA, 'd'.repeat(64))
  await assert.rejects(extract(repo, store, BUILD, false, cache), /is not the zip tracked/)
  assert.equal(repo.readBuild(BUILD), undefined)
})

void test('a build not tracked is refused', async () => {
  const { repo, store, cache } = setup()
  await assert.rejects(extract(repo, store, '1.26.53.1', false, cache), /not tracked: pnpm track 1\.26\.53\.1/)
})
