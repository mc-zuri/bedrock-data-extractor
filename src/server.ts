import { spawn, type ChildProcessByStdio } from 'node:child_process'
import { createSocket } from 'node:dgram'
import { chmodSync, existsSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import type { Readable, Writable } from 'node:stream'
import { SERVER_PORT_BASE } from './config.ts'
import type { Host } from './platform.ts'
import { compareVersions } from './versions.ts'
import { unpack } from './zip.ts'

const LEVEL = 'bdg'
const START_TIMEOUT_MS = 200_000
const COMMAND_TIMEOUT_MS = 60_000

/** `zip` into `dir`, its server made executable (some zips keep no Unix modes: 1.16.220, 1.17.0). */
export async function unpackServer(zip: string, dir: string, host: Host): Promise<void> {
  rmSync(dir, { recursive: true, force: true })
  await unpack(zip, dir)
  const exe = join(dir, host.serverExe)

  if (!existsSync(exe)) {
    throw new Error(`${zip} has no ${host.serverExe}`)
  }

  if (host.platform === 'linux') {
    chmodSync(exe, 0o755)
  }
}

// the ports of the servers running, by their first (IPv4; IPv6 is the next)
const taken = new Set<number>()

export type Transport = 'raknet' | 'nethernet'

// From 1.26.52 a server takes no RakNet client ("NetherNet is the only supported transport type").
const NETHERNET_FROM = '1.26.52'

export function transportOf(build: string): Transport {
  return compareVersions(build, NETHERNET_FROM) >= 0 ? 'nethernet' : 'raknet'
}

// A NetherNet server is found on the one LAN port all of them bind (7551): one runs at a time, each after the one
// before has exited.
let lanPortFree: Promise<void> = Promise.resolve()

/** Waits for the LAN port; resolves with what frees it again. */
async function takeLanPort(): Promise<() => void> {
  const before = lanPortFree
  const { promise, resolve } = Promise.withResolvers<void>()
  lanPortFree = promise
  await before
  return resolve
}

function isFree(port: number, type: 'udp4' | 'udp6'): Promise<boolean> {
  return new Promise((resolve) => {
    const socket = createSocket({ type, ipv6Only: type === 'udp6' })
    socket.once('error', () => resolve(false))
    socket.bind(port, () => socket.close(() => resolve(true)))
  })
}

async function takePort(): Promise<number> {
  for (let port = SERVER_PORT_BASE; port < SERVER_PORT_BASE + 2000; port += 2) {
    if (taken.has(port)) {
      continue
    }

    taken.add(port)

    if ((await isFree(port, 'udp4')) && (await isFree(port + 1, 'udp6'))) {
      return port
    }

    taken.delete(port)
  }

  throw new Error(`no two free ports from ${SERVER_PORT_BASE} on`)
}

function configure(dir: string, port: number, transport: Transport): void {
  const file = join(dir, 'server.properties')

  if (!existsSync(file)) {
    throw new Error(`no ${file}`)
  }

  const properties = {
    'level-name': LEVEL,
    'server-port': port,
    'server-portv6': port + 1,
    // a NetherNet client finds the server on the LAN
    'enable-lan-visibility': transport === 'nethernet',
    'online-mode': false,
    'allow-list': false,
    // the same biome (plains) wherever blocks are set: a map colour that follows the biome follows no seed
    'level-type': 'FLAT',
    transport,
  }
  let content = readFileSync(file, 'utf8')

  for (const [key, value] of Object.entries(properties)) {
    const line = `${key}=${value}`
    const existing = new RegExp(`^${key}=[^\r\n]*`, 'm') // keeps the file's line endings
    content = existing.test(content) ? content.replace(existing, line) : content.replace(/\r?\n?$/, `\n${line}\n`)
  }

  writeFileSync(file, content)
}

export interface RunningServer {
  pid: number
  /** its IPv4 port */
  port: number
  transport: Transport
  exited: Promise<number | null>
  kill(): Promise<void>
  /** Types `line` into its console; resolves with which of `answers` it prints first. */
  command(line: string, answers: string[]): Promise<string>
  /** Types `line` into its console; resolves with what it prints from then on, once `ended` holds for that and it prints nothing more for `quietMs`. */
  answer(line: string, ended: (said: string) => boolean, quietMs: number): Promise<string>
}

export async function start(dir: string, host: Host, env: Record<string, string>, transport: Transport): Promise<RunningServer> {
  const freeLanPort = transport === 'nethernet' ? await takeLanPort() : (): void => {}
  const port = await takePort()

  try {
    configure(dir, port, transport)
    rmSync(join(dir, 'worlds', LEVEL), { recursive: true, force: true })
  } catch (e) {
    taken.delete(port)
    freeLanPort()
    throw e
  }

  const child = spawn(join(dir, host.serverExe), [], { cwd: dir, stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, ...host.serverEnv, ...env } })
  child.stdin.on('error', () => {})
  child.stderr.resume()
  const exited = new Promise<number | null>((resolve) => child.once('exit', resolve))
  void exited.then(() => {
    taken.delete(port)
    freeLanPort()
  })
  async function kill(): Promise<void> {
    // one that could not be started never exits
    if (child.pid === undefined) {
      taken.delete(port)
      freeLanPort()
      return
    }

    if (child.exitCode === null && child.signalCode === null) {
      host.kill(child)
    }

    await exited
  }

  try {
    await untilOutput(child, ['Server started'], START_TIMEOUT_MS)
  } catch (e) {
    await kill()
    throw e
  }

  if (child.pid === undefined) {
    throw new Error('the server has no process id')
  }

  function command(line: string, answers: string[]): Promise<string> {
    const answered = untilOutput(child, answers, COMMAND_TIMEOUT_MS)
    child.stdin.write(`${line}\n`)
    return answered
  }

  function answer(line: string, ended: (said: string) => boolean, quietMs: number): Promise<string> {
    return new Promise((resolve, reject) => {
      let said = ''
      let quiet: NodeJS.Timeout | undefined
      const timeout = setTimeout(() => settle(new Error(`no answer to ${line} within ${COMMAND_TIMEOUT_MS / 1000} s: ${said.trim().split('\n').at(-1)}`)), COMMAND_TIMEOUT_MS)
      function settle(error?: Error): void {
        clearTimeout(timeout)
        clearTimeout(quiet)
        child.stdout.off('data', listen)
        child.off('exit', exit)

        if (error) {
          reject(error)
        } else {
          resolve(said)
        }
      }
      function exit(code: number | null): void {
        settle(new Error(`exited with code ${code} while answering ${line}`))
      }
      function listen(data: Buffer): void {
        said += data.toString()
        clearTimeout(quiet)

        if (ended(said)) {
          quiet = setTimeout(settle, quietMs)
        }
      }
      child.stdout.on('data', listen)
      child.once('exit', exit)
      child.stdin.write(`${line}\n`)
    })
  }

  return { pid: child.pid, port, transport, exited, kill, command, answer }
}

/** Resolves with the first of `texts` the server prints. */
function untilOutput(child: ChildProcessByStdio<Writable, Readable, Readable>, texts: string[], timeoutMs: number): Promise<string> {
  return new Promise((resolve, reject) => {
    let tail = ''
    const timer = setTimeout(() => settle(new Error(`none of ${texts.join(', ')} within ${timeoutMs / 1000} s: ${tail.trim().split('\n').at(-1)}`)), timeoutMs)
    function settle(error?: Error, text = ''): void {
      clearTimeout(timer)
      child.stdout.off('data', listen)
      child.off('error', settle)
      child.off('exit', exit)
      child.stdout.resume()

      if (error) {
        reject(error)
      } else {
        resolve(text)
      }
    }
    function listen(data: Buffer): void {
      tail = (tail + data.toString()).slice(-4096)
      const text = texts.find((t) => tail.includes(t))

      if (text !== undefined) {
        settle(undefined, text)
      }
    }
    function exit(code: number | null): void {
      settle(new Error(`exited with code ${code} before ${texts.join(', ')}:\n${tail}`))
    }
    child.stdout.on('data', listen)
    child.once('error', settle)
    child.once('exit', exit)
  })
}
