import { existsSync, mkdirSync, readFileSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { setTimeout as sleep } from 'node:timers/promises'
import type { Host } from './platform.ts'
import { start } from './server.ts'

const AGENT_TIMEOUT_MS = 600_000

export async function runAgent(serverDir: string, build: string, host: Host, out: string): Promise<string> {
  rmSync(out, { recursive: true, force: true })
  mkdirSync(out, { recursive: true })

  const result = join(out, 'result.txt')
  const server = await start(serverDir, host, { ...host.agentEnv, BDG_VERSION: build, BDG_OUT: out })

  try {
    await host.attach(server.pid, out)
    await waitForFile(result, server.exited, AGENT_TIMEOUT_MS)
  } finally {
    await server.kill()
  }

  const text = readFileSync(result, 'utf8').trim()

  if (!text.startsWith('ok')) {
    throw new Error(`the agent: ${text.replace(/^FAILED: /, '')} (see ${join(out, 'agent.log')})`)
  }

  return text.replace(/^ok /, '')
}

async function waitForFile(file: string, exited: Promise<number | null>, timeoutMs: number): Promise<void> {
  let exitCode: number | null | undefined

  void exited.then((code) => {
    exitCode = code
  })

  const deadline = Date.now() + timeoutMs

  while (!existsSync(file)) {
    if (exitCode !== undefined) {
      throw new Error(`the server exited (code ${exitCode}) before the agent finished`)
    }

    if (Date.now() > deadline) {
      throw new Error(`the agent did not finish within ${timeoutMs / 1000} s`)
    }

    await sleep(500)
  }
}
