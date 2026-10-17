import { spawn, type ChildProcessByStdio } from 'node:child_process'
import { createSocket } from 'node:dgram'
import { chmodSync, existsSync, readFileSync, rmSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import type { Readable, Writable } from 'node:stream'
import { SERVER_PORT_BASE } from './config.ts'
import type { Host } from './platform.ts'
import { unpack } from './zip.ts'

const LEVEL = 'bdg'
const START_TIMEOUT_MS = 200_000

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

/** Whether no other program has `port` now. */
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

function configure(dir: string, port: number): void {
  const file = join(dir, 'server.properties')

  if (!existsSync(file)) {
    throw new Error(`no ${file}`)
  }

  const properties = {
    'level-name': LEVEL,
    'server-port': port,
    'server-portv6': port + 1,
    'enable-lan-visibility': false,
    'online-mode': false,
    'allow-list': false,
    transport: 'raknet',
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
  exited: Promise<number | null>
  kill(): Promise<void>
}

export async function start(dir: string, host: Host, env: Record<string, string>): Promise<RunningServer> {
  const port = await takePort()

  try {
    configure(dir, port)
    rmSync(join(dir, 'worlds', LEVEL), { recursive: true, force: true })
  } catch (e) {
    taken.delete(port)
    throw e
  }

  const child = spawn(join(dir, host.serverExe), [], { cwd: dir, stdio: ['pipe', 'pipe', 'pipe'], env: { ...process.env, ...host.serverEnv, ...env } })
  child.stdin.on('error', () => {})
  child.stderr.resume()
  const exited = new Promise<number | null>((resolve) => child.once('exit', resolve))
  void exited.then(() => taken.delete(port))
  async function kill(): Promise<void> {
    if (child.pid === undefined) {
      taken.delete(port)
      return
    }

    if (child.exitCode === null && child.signalCode === null) {
      host.kill(child)
    }

    await exited
  }

  try {
    await untilOutput(child, 'Server started', START_TIMEOUT_MS)
  } catch (e) {
    await kill()
    throw e
  }

  if (child.pid === undefined) {
    throw new Error('the server has no process id')
  }

  return { pid: child.pid, exited, kill }
}

function untilOutput(child: ChildProcessByStdio<Writable, Readable, Readable>, text: string, timeoutMs: number): Promise<void> {
  return new Promise((resolve, reject) => {
    let tail = ''
    const timer = setTimeout(() => settle(new Error(`no "${text}" within ${timeoutMs / 1000} s: ${tail.trim().split('\n').at(-1)}`)), timeoutMs)
    function settle(error?: Error): void {
      clearTimeout(timer)
      child.stdout.off('data', listen)
      child.stdout.resume()

      if (error) {
        reject(error)
      } else {
        resolve()
      }
    }
    function listen(data: Buffer): void {
      tail = (tail + data.toString()).slice(-4096)

      if (tail.includes(text)) {
        settle()
      }
    }
    child.stdout.on('data', listen)
    child.once('error', settle)
    child.once('exit', (code) => settle(new Error(`exited with code ${code} before "${text}":\n${tail}`)))
  })
}
