import assert from 'node:assert/strict'
import { test } from 'node:test'
import { transportOf } from '../src/server.ts'

void test('a client joins over RakNet, from 1.26.52 over NetherNet', () => {
  assert.equal(transportOf('1.16.201.02'), 'raknet')
  assert.equal(transportOf('1.26.51.1'), 'raknet')
  assert.equal(transportOf('1.26.52.3'), 'nethernet')
  assert.equal(transportOf('1.26.100.1'), 'nethernet')
})
