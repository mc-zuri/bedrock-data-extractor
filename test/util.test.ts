import assert from 'node:assert/strict'
import { test } from 'node:test'
import { forEach } from '../src/util.ts'

void test('the first failure stops the rest: none is started after it, and the running ones are told to stop', async () => {
  const started: string[] = []
  const stopped: string[] = []
  async function work(item: string, signal: AbortSignal): Promise<string> {
    started.push(item)

    if (item === 'b') {
      throw new Error('b fails')
    }

    // a runs until it is told to stop
    await new Promise((_, reject) => {
      signal.addEventListener('abort', () => {
        stopped.push(item)
        reject(new Error('stopped'))
      })
    })
    return 'done'
  }

  assert.deepEqual(await forEach(['a', 'b', 'c', 'd'], 2, (item) => item, work), ['b'])
  assert.deepEqual(started, ['a', 'b'])
  assert.deepEqual(stopped, ['a'])
})
