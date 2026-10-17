import { execFile, spawnSync, type ChildProcess } from 'node:child_process'
import { existsSync, writeFileSync } from 'node:fs'
import { join } from 'node:path'
import { promisify } from 'node:util'
import { NATIVE_DIR } from './config.ts'
import type { Platform } from './versions.ts'

export interface Host {
  platform: Platform
  serverExe: string
  serverEnv: Record<string, string>
  kill(child: ChildProcess): void
  agentFiles: string[]
  buildAgent(): void
  agentEnv: Record<string, string>
  attach(pid: number, out: string): Promise<void>
  check(exe: string, build: string, args?: string[]): { ok: boolean; output: string }
}

function run(cmd: string, args: string[], cwd?: string): void {
  const r = spawnSync(cmd, args, { cwd, stdio: 'inherit' })

  if (r.error) {
    throw new Error(`${cmd}: ${r.error.message}`)
  }

  if (r.status !== 0) {
    throw new Error(`${cmd} ${args.join(' ')} exited with ${r.status}`)
  }
}

function check(tool: string, args: string[]): { ok: boolean; output: string } {
  const r = spawnSync(tool, args, { encoding: 'utf8' })
  return { ok: r.status === 0, output: `${r.stdout ?? ''}${r.stderr ?? ''}`.trim() }
}

const windowsBin = join(NATIVE_DIR, 'build', 'windows', 'x64', 'release')
const linuxBin = join(NATIVE_DIR, 'build', 'linux', 'x86_64', 'release')

const windows: Host = {
  platform: 'windows',
  serverExe: 'bedrock_server.exe',
  serverEnv: {},
  kill(child) {
    if (child.pid) {
      spawnSync('taskkill', ['/pid', String(child.pid), '/f', '/t'], { stdio: 'ignore' })
    }
  },
  agentFiles: ['bdg_agent.dll', 'bdg_inject.exe', 'bdg_check.exe'].map((f) => join(windowsBin, f)),
  buildAgent() {
    run('xmake', ['f', '-p', 'windows', '-a', 'x64', '-m', 'release', '-y', '-q'], NATIVE_DIR)
    run('xmake', ['-y', '-q'], NATIVE_DIR)
  },
  agentEnv: {},
  async attach(pid) {
    await promisify(execFile)(join(windowsBin, 'bdg_inject.exe'), [String(pid), join(windowsBin, 'bdg_agent.dll')]).catch((e: { stderr?: string; message: string }) => {
      throw new Error(`could not inject the agent: ${e.stderr?.trim() || e.message}`)
    })
  },
  check: (exe, build, args = []) => check(join(windowsBin, 'bdg_check.exe'), [exe, build, ...args]),
}

const linux: Host = {
  platform: 'linux',
  serverExe: 'bedrock_server',
  serverEnv: { LD_LIBRARY_PATH: ['.', process.env.LD_LIBRARY_PATH].filter(Boolean).join(':') },
  kill(child) {
    child.kill('SIGKILL')
  },
  agentFiles: ['libbdg_agent.so', 'bdg_check'].map((f) => join(linuxBin, f)),
  buildAgent() {
    run('xmake', ['f', '-p', 'linux', '-a', 'x86_64', '-m', 'release', '--toolchain=clang', '-y', '-q'], NATIVE_DIR)
    run('xmake', ['-y', '-q'], NATIVE_DIR)
  },
  agentEnv: { LD_PRELOAD: join(linuxBin, 'libbdg_agent.so') },
  attach(_pid, out) {
    writeFileSync(join(out, 'go'), '')
    return Promise.resolve()
  },
  check: (exe, build, args = []) => check(join(linuxBin, 'bdg_check'), [exe, build, ...args]),
}

export function hostPlatform(os: NodeJS.Platform = process.platform): Platform | undefined {
  return os === 'win32' ? 'windows' : os === 'linux' ? 'linux' : undefined
}

export function host(platform: Platform | undefined = hostPlatform()): Host {
  if (!platform || platform !== hostPlatform()) {
    const where = platform === 'linux' ? 'Linux (on Windows: WSL)' : platform === 'windows' ? 'Windows' : 'Windows or Linux'
    throw new Error(`${platform ?? 'a'} server runs on ${where}, not ${process.platform}`)
  }

  return platform === 'windows' ? windows : linux
}

export function agentHost(platform?: Platform): Host {
  const h = host(platform)
  const missing = h.agentFiles.find((f) => !existsSync(f))

  if (missing) {
    throw new Error(`no ${missing}: pnpm native`)
  }

  return h
}
