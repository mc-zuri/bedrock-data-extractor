import assert from 'node:assert/strict'
import { test } from 'node:test'
import { parseLinks } from '../src/mojang.ts'

// the API's answer on 2026-10-17
const ANSWER = {
  result: {
    links: [
      { downloadType: 'serverBedrockWindows', downloadUrl: 'https://www.minecraft.net/bedrockdedicatedserver/bin-win/bedrock-server-1.26.52.3.zip' },
      { downloadType: 'serverBedrockLinux', downloadUrl: 'https://www.minecraft.net/bedrockdedicatedserver/bin-linux/bedrock-server-1.26.52.3.zip' },
      { downloadType: 'serverBedrockPreviewWindows', downloadUrl: 'https://www.minecraft.net/bedrockdedicatedserver/bin-win-preview/bedrock-server-1.26.70.20.zip' },
      { downloadType: 'serverBedrockPreviewLinux', downloadUrl: 'https://www.minecraft.net/bedrockdedicatedserver/bin-linux-preview/bedrock-server-1.26.70.20.zip' },
      { downloadType: 'serverJar', downloadUrl: 'https://piston-data.mojang.com/v1/objects/0000000000000000000000000000000000000000/server.jar' },
    ],
  },
}

void test("the links name each channel and platform's latest build; the Java server is not one", () => {
  assert.deepEqual(parseLinks(ANSWER), [
    { channel: 'release', platform: 'windows', build: '1.26.52.3' },
    { channel: 'release', platform: 'linux', build: '1.26.52.3' },
    { channel: 'preview', platform: 'windows', build: '1.26.70.20' },
    { channel: 'preview', platform: 'linux', build: '1.26.70.20' },
  ])
})

void test('a link somewhere zipUrl would not look is refused', () => {
  const moved = { result: { links: [{ downloadType: 'serverBedrockLinux', downloadUrl: 'https://cdn.example.com/bin-linux/bedrock-server-1.26.52.3.zip' }] } }
  assert.throws(() => parseLinks(moved), /not a zip where/)
})

void test('an answer without links, or without a release, is refused', () => {
  assert.throws(() => parseLinks({ error: 'nope' }), /no result\.links/)
  assert.throws(() => parseLinks({ result: { links: [ANSWER.result.links[3]] } }), /no release server/)
})
