import assert from 'node:assert/strict'
import { test } from 'node:test'
import { host, hostPlatform } from '../src/platform.ts'

void test("a machine runs its own OS's servers only", () => {
  assert.equal(hostPlatform('win32'), 'windows')
  assert.equal(hostPlatform('linux'), 'linux')
  assert.equal(hostPlatform('darwin'), undefined)
  const mine = hostPlatform()

  if (!mine) {
    return
  }

  assert.equal(host(mine).platform, mine)
  const other = mine === 'linux' ? 'windows' : 'linux'
  assert.throws(() => host(other), new RegExp(`${other} server runs on`))
})
