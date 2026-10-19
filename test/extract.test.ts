import assert from 'node:assert/strict'
import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { test } from 'node:test'
import { gunzipSync } from 'node:zlib'
import type { BlockShapes } from '../src/blocks.ts'
import { DataRepo } from '../src/dataRepo.ts'
import { extract, isExtracted } from '../src/extract.ts'
import type { Language } from '../src/language.ts'
import { cachedRelease, emptyDataRepo, fakeBlocks, sampleBlocks, VANILLA } from './helpers.ts'

const BUILD = '1.26.52.3'

function gunzipped(repo: DataRepo, file: string): Buffer {
  return gunzipSync(readFileSync(join(repo.buildDir(BUILD), file)))
}

void test("a release's data is read out of its supported linux zip, and its zips kept with it", async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: VANILLA }, ['linux', 'windows'])
  const repo = new DataRepo(emptyDataRepo())
  const blocks = fakeBlocks()
  assert.equal(
    await extract(repo, store, BUILD, false, cache, blocks),
    'build.json, language.json.gz, block_palette.nbt, block_shapes.json.gz, blocks.json.gz, items.json.gz, enchantments.json.gz, biomes.json.gz, effects.json.gz, enums.json.gz, tints.json.gz, recipes.json.gz, entities.json.gz, commands.json.gz, attributes.json.gz written (from linux: 6000 vanilla strings, 509 block states)',
  )
  assert.deepEqual(blocks.calls, [{ build: BUILD, platform: 'linux' }])
  const language = JSON.parse(gunzipped(repo, 'language.json.gz').toString('utf8')) as Language
  assert.equal(Object.keys(language.packs.vanilla).length, 6000)
  assert.deepEqual(language.packs.chemistry, { 'item.element_1.name': 'Hydrogen' })
  assert.deepEqual(gunzipped(repo, 'block_palette.nbt'), sampleBlocks().palette, 'the palette is the NBT the agent wrote')
  const shapes = JSON.parse(gunzipped(repo, 'block_shapes.json.gz').toString('utf8')) as BlockShapes
  assert.deepEqual(shapes, sampleBlocks().shapes)
  assert.deepEqual(JSON.parse(gunzipped(repo, 'blocks.json.gz').toString('utf8')), sampleBlocks().blocks)
  assert.deepEqual(JSON.parse(gunzipped(repo, 'items.json.gz').toString('utf8')), sampleBlocks().items)

  for (const kind of ['biomes', 'effects', 'enums', 'tints', 'recipes', 'entities', 'commands', 'attributes'] as const) {
    assert.deepEqual(JSON.parse(gunzipped(repo, `${kind}.json.gz`).toString('utf8')), sampleBlocks()[kind], kind)
  }

  const version = store.read('release', BUILD)
  const json = repo.readBuild(BUILD)
  assert.deepEqual(json?.binary.linux, { url: version?.binary.linux?.url, sha256: version?.binary.linux?.sha256, size: version?.binary.linux?.size })
  assert.deepEqual(json?.data, {
    language: { file: 'language.json.gz', from: 'linux' },
    blockPalette: { file: 'block_palette.nbt', from: 'linux' },
    blockShapes: { file: 'block_shapes.json.gz', from: 'linux' },
    blocks: { file: 'blocks.json.gz', from: 'linux' },
    items: { file: 'items.json.gz', from: 'linux' },
    enchantments: { file: 'enchantments.json.gz', from: 'linux' },
    biomes: { file: 'biomes.json.gz', from: 'linux' },
    effects: { file: 'effects.json.gz', from: 'linux' },
    enums: { file: 'enums.json.gz', from: 'linux' },
    tints: { file: 'tints.json.gz', from: 'linux' },
    recipes: { file: 'recipes.json.gz', from: 'linux' },
    entities: { file: 'entities.json.gz', from: 'linux' },
    commands: { file: 'commands.json.gz', from: 'linux' },
    attributes: { file: 'attributes.json.gz', from: 'linux' },
  })
  assert.ok(!existsSync(join(cache, 'release', BUILD)), 'its zips are deleted once it is done')
})

void test('a release supported only on windows is read out of its windows zip', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA, windows: VANILLA }, ['windows'])
  const repo = new DataRepo(emptyDataRepo())
  assert.match((await extract(repo, store, BUILD, false, cache, fakeBlocks())) ?? '', /from windows/)
})

void test('a release supported on no platform is refused', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA })
  await assert.rejects(extract(new DataRepo(emptyDataRepo()), store, BUILD, false, cache, fakeBlocks()), /supported on no platform: pnpm support 1\.26\.52\.3/)
})

void test('a release bedrock-data has every kind of is left as it is, unless forced', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  await extract(repo, store, BUILD, false, cache, fakeBlocks())
  assert.ok(isExtracted(repo, BUILD))
  assert.equal(await extract(repo, store, BUILD, false, cache, fakeBlocks()), undefined)
})

void test('a release bedrock-data has only some kinds of is extracted again', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  await extract(repo, store, BUILD, false, cache, fakeBlocks())
  const json = repo.readBuild(BUILD)

  if (!json) {
    throw new Error('no build.json')
  }

  repo.writeBuild({ ...json, data: { language: json.data.language } })
  assert.ok(!isExtracted(repo, BUILD))
  // its zip was deleted once it was done: the same release cached again
  const again = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  assert.match((await extract(repo, again.store, BUILD, false, again.cache, fakeBlocks())) ?? '', /^build\.json written/)
})

void test('data that fails the checks is not written', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: 'tile.stone.name=Stone\n' }, ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  await assert.rejects(extract(repo, store, BUILD, false, cache, fakeBlocks()), /the vanilla pack has 1 strings/)
  assert.equal(repo.readBuild(BUILD), undefined)
})

void test('a release whose blocks cannot be read is not written, and its zip is kept', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  await assert.rejects(extract(repo, store, BUILD, false, cache, fakeBlocks(new Error('the agent: no collision shape for minecraft:stone #0'))), /no collision shape/)
  assert.equal(repo.readBuild(BUILD), undefined)
  assert.ok(existsSync(join(cache, 'release', BUILD, 'linux.zip')))
})

void test('a zip that is not the one tracked is refused, and nothing is written', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'], ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  await assert.rejects(extract(repo, store, BUILD, false, cache, fakeBlocks()), /is not the zip tracked/)
  assert.equal(repo.readBuild(BUILD), undefined)
})

void test('a build not tracked is refused', async () => {
  const { store, cache } = cachedRelease(BUILD, { linux: VANILLA }, ['linux'])
  await assert.rejects(extract(new DataRepo(emptyDataRepo()), store, '1.26.53.1', false, cache, fakeBlocks()), /not tracked: pnpm track 1\.26\.53\.1/)
})

void test("a Linux server's shapes before 1.26.20 are written as the Windows server gives them: the chests' top 0.95", async () => {
  const build = '1.26.10.4'
  const { store, cache } = cachedRelease(build, { linux: VANILLA }, ['linux'])
  const repo = new DataRepo(emptyDataRepo())
  await extract(repo, store, build, false, cache, fakeBlocks())
  const shapes = JSON.parse(gunzipSync(readFileSync(join(repo.buildDir(build), 'block_shapes.json.gz'))).toString('utf8')) as BlockShapes
  assert.deepEqual(shapes.blocks.at(-1)?.states[0].collision, [[0.025, 0, 0.025, 0.975, 0.95, 0.975]])
})
