import assert from 'node:assert/strict'
import { test } from 'node:test'
import { checkCommands } from '../src/checks.ts'
import { helpOf } from '../src/commandHelp.ts'
import type { CommandsFile } from '../src/sent.ts'

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

void test("a command's help that does not start with its name, or holds what no console writes, is refused", () => {
  function command(name: string, header: string, aliases: string[] = []): CommandsFile['commands'][number] {
    return { name, description: `commands.${name}.description`, aliases, help: [header, 'Usage:', `- /${name}`] }
  }
  const commands = [command('teleport', 'teleport (also tp):', ['tp']), ...Array.from({ length: 40 }, (_, i) => command(`command_${i}`, `command_${i}:`))]
  const enums = { GameMode: ['survival'] }
  checkCommands({ commands, enums })
  // a § the pipe split, read as two characters
  assert.throws(() => checkCommands({ commands: [...commands, command('tag', '\uFFFD\uFFFDetag:')], enums }), /help of tag is garbled/)
  assert.throws(() => checkCommands({ commands: [...commands, command('tag', 'Manages tags stored in entities.')], enums }), /help of tag is garbled/)
})
