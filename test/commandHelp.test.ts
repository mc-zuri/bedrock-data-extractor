import assert from 'node:assert/strict'
import { test } from 'node:test'
import { helpOf } from '../src/commandHelp.ts'

void test("a command's help: from its header to its last usage line, without the log's prefix, the colours and what the server logs of a player", () => {
  const said = [
    '[2026-10-18 14:55:26:894 INFO] Server started.',
    '§eteleport (also tp):',
    '§eTeleports entities (players, mobs, etc.).',
    '[2026-10-18 14:55:27:001 INFO] Player disconnected: bdg, xuid: , pfid: ',
    'Usage:',
    '- /teleport <destination: target>',
    '- /teleport <destination: x y z> [checkForBlocks: Boolean]',
    '[2026-10-18 14:55:27:002 INFO] Running AutoCompaction...',
  ].join('\n')
  assert.deepEqual(helpOf('teleport', said), [
    'teleport (also tp):',
    'Teleports entities (players, mobs, etc.).',
    'Usage:',
    '- /teleport <destination: target>',
    '- /teleport <destination: x y z> [checkForBlocks: Boolean]',
  ])
})
