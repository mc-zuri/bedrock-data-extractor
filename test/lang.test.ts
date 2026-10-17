import assert from 'node:assert/strict'
import { test } from 'node:test'
import { parseLang } from '../src/lang.ts'

function parsed(...lines: string[]): Record<string, string> {
  return Object.fromEntries(parseLang(lines.join('\r\n')))
}

void test('a string keeps a second = and a # that starts no comment', () => {
  assert.deepEqual(parsed('advMode.allEntities=@e = all entities', 'gameTip.hotbar.touch=Tap # in hotbar to hold it'), {
    'advMode.allEntities': '@e = all entities',
    'gameTip.hotbar.touch': 'Tap # in hotbar to hold it',
  })
  assert.deepEqual(parsed('playscreen.x=reach out in #ask-Realms-preprod-access or see aka.ms/x.'), { 'playscreen.x': 'reach out in #ask-Realms-preprod-access or see aka.ms/x.' })
})

void test('an inline comment starts at ## or at a tab followed by #', () => {
  assert.deepEqual(
    parsed(
      'accessibility.index= %s of %s\t## This is numbering',
      "raytracing.RTX.name=RTX##Brand name, don't localize",
      'store.realmsPlus.buyNow=SUBSCRIBE NOW ## Maximum 13 characters',
      'codeScreen.deprecatedIDE.button.close=Close###{MaxLength=27}',
      'store.mashup.count.worlds=%s Worlds\t#number of worlds',
      'options.dev_enableDebugHudOverlay.hbui=Gameface \t\t\t\t# This is a proper name',
      'options.pauseHeader=Pause\t#',
      'x.tabThenBlanks=Value\t  # note',
    ),
    {
      'accessibility.index': ' %s of %s',
      'raytracing.RTX.name': 'RTX',
      'store.realmsPlus.buyNow': 'SUBSCRIBE NOW',
      'codeScreen.deprecatedIDE.button.close': 'Close',
      'store.mashup.count.worlds': '%s Worlds',
      'options.dev_enableDebugHudOverlay.hbui': 'Gameface',
      'options.pauseHeader': 'Pause',
      'x.tabThenBlanks': 'Value',
    },
  )
})

void test('comment lines, blank lines and lines without a key say nothing', () => {
  assert.deepEqual(parsed('## a comment', '   # indented comment', '', '   ', 'no equals sign here', '=no key', 'a.b=c'), { 'a.b': 'c' })
})

void test('a byte order mark, \\r\\n and the spaces around a key are dropped; a leading space in a string is kept', () => {
  assert.deepEqual(Object.fromEntries(parseLang('﻿  key.one =  two \r\nkey.two=x\n')), { 'key.one': '  two', 'key.two': 'x' })
})

void test('a key said twice keeps its first place and its last string', () => {
  assert.deepEqual(
    [...parseLang('a=1\nb=2\na=3\n')],
    [
      ['a', '3'],
      ['b', '2'],
    ],
  )
})
