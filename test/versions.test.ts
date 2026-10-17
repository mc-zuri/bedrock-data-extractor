import assert from 'node:assert/strict'
import { test } from 'node:test'
import { compareVersions, isBuild, versionOf, zipUrl } from '../src/versions.ts'

void test('builds compare part by part as numbers', () => {
  assert.ok(compareVersions('1.21.42.01', '1.21.80.3') < 0)
  assert.ok(compareVersions('1.21.100.6', '1.21.93.1') > 0)
  assert.equal(compareVersions('1.21.42.01', '1.21.42.1'), 0)
  assert.deepEqual(['1.26.0.2', '1.21.130.3', '1.26.10.4', '1.9.0.15'].sort(compareVersions), ['1.9.0.15', '1.21.130.3', '1.26.0.2', '1.26.10.4'])
})

void test('a build is four numbers', () => {
  assert.ok(isBuild('1.21.42.01'))
  assert.ok(!isBuild('1.21.42'))
  assert.ok(!isBuild('1.21.42.01.zip'))
})

void test("a release's version drops its build; a preview's makes it the preview's number", () => {
  assert.equal(versionOf('1.26.52.3', 'release'), '1.26.52')
  assert.equal(versionOf('1.21.42.01', 'release'), '1.21.42')
  assert.equal(versionOf('1.26.70.20', 'preview'), '1.26.70-preview.20')
})

void test('each channel and platform has its zip folder', () => {
  assert.equal(zipUrl('1.21.42.01', 'release', 'linux'), 'https://www.minecraft.net/bedrockdedicatedserver/bin-linux/bedrock-server-1.21.42.01.zip')
  assert.equal(zipUrl('1.26.52.3', 'release', 'windows'), 'https://www.minecraft.net/bedrockdedicatedserver/bin-win/bedrock-server-1.26.52.3.zip')
  assert.equal(zipUrl('1.26.70.20', 'preview', 'linux'), 'https://www.minecraft.net/bedrockdedicatedserver/bin-linux-preview/bedrock-server-1.26.70.20.zip')
  assert.equal(zipUrl('1.26.70.20', 'preview', 'windows'), 'https://www.minecraft.net/bedrockdedicatedserver/bin-win-preview/bedrock-server-1.26.70.20.zip')
})
