import { existsSync, mkdirSync, readFileSync, rmSync } from 'node:fs'
import { join } from 'node:path'
import { setTimeout as sleep } from 'node:timers/promises'
import { joinServer, type Sent } from './client.ts'
import { commandHelp } from './commandHelp.ts'
import type { Host } from './platform.ts'
import { start, transportOf, type RunningServer } from './server.ts'

// the slowest builds draw their drops for minutes (1.20.61: ten alone, more beside other servers)
const AGENT_TIMEOUT_MS = 1_800_000
// for the ticking area's chunks to load
const CHUNK_LOAD_MS = 3_000

const TICKING_AREA = 'tickingarea add 1024 0 1024 1039 0 1039 bdg'
const ADDED = ['Added ticking area', 'already exists']

// The agent sets blocks to draw their drops and ask their map colours, in a chunk a ticking area keeps loaded (a server
// without players loads none), far from the world origin, where the shapes are asked with no chunk around
// (native/game/Drops.cpp). Before 1.17.0 the console runs no command in a dimension: the joined player runs it.
async function keepChunksLoaded(server: RunningServer): Promise<void> {
  for (let tries = 0; ; tries++) {
    const said = await server.command(`execute @a ~ ~ ~ ${TICKING_AREA}`, [...ADDED, 'No targets'])

    if (said !== 'No targets') {
      return
    }

    if (tries === 30) {
      throw new Error('the joined player never came into the world')
    }

    await sleep(1_000)
  }
}

export interface AgentRun {
  /** what the agent wrote ("ok ..." without its ok) */
  result: string
  sent: Sent
  /** the console's help for each command the client was sent */
  help: Record<string, string[]>
}

/** One run of the agent in the server at `serverDir`; `signal` ends the server, failing the run. */
export async function runAgent(serverDir: string, build: string, host: Host, out: string, signal?: AbortSignal): Promise<AgentRun> {
  signal?.throwIfAborted()
  rmSync(out, { recursive: true, force: true })
  mkdirSync(out, { recursive: true })

  const result = join(out, 'result.txt')
  const server = await start(serverDir, host, { ...host.agentEnv, BDG_VERSION: build, BDG_OUT: out }, transportOf(build))

  function stop(): void {
    void server.kill()
  }

  signal?.addEventListener('abort', stop, { once: true })

  try {
    signal?.throwIfAborted()
    const inDimension = (await server.command(TICKING_AREA, [...ADDED, 'dimension not found'])) !== 'dimension not found'
    const sent = await joinServer(build, server, async () => {
      if (!inDimension) {
        await keepChunksLoaded(server)
      }
    })
    // before the agent: after the long draws of 1.16.201's (unoptimized) server, its console answers no more
    const help = await commandHelp(
      server,
      sent.commands.commands.map((c) => c.name),
    )
    await sleep(CHUNK_LOAD_MS)
    await host.attach(server.pid, out)
    await waitForFile(result, server.exited, AGENT_TIMEOUT_MS)
    const text = readFileSync(result, 'utf8').trim()

    if (!text.startsWith('ok')) {
      throw new Error(`the agent: ${text.replace(/^FAILED: /, '')} (see ${join(out, 'agent.log')})`)
    }

    return { result: text.replace(/^ok /, ''), sent, help }
  } finally {
    signal?.removeEventListener('abort', stop)
    await server.kill()
  }
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
