import { rmSync } from 'node:fs'
import { join } from 'node:path'
import { readBlocks } from './blocks.ts'
import { BEDROCK_DATA_DIR, CACHE_DIR, JOBS, SCHEMAS_DIR, VERSIONS_DIR } from './config.ts'
import { DataRepo } from './dataRepo.ts'
import { checkSteve } from './checks.ts'
import { exportPlatform, extract, isExtracted, trackedZip, workDir } from './extract.ts'
import { fetchLinks } from './mojang.ts'
import { agentHost, host, hostPlatform } from './platform.ts'
import { unpackServer } from './server.ts'
import { importedSkin, recordedSkin, STEVE_FILE } from './steve.ts'
import { support } from './support.ts'
import { isTracked, targetName, track, type Target } from './track.ts'
import { errorMessage, forEach } from './util.ts'
import { VersionStore } from './versionStore.ts'
import { CHANNELS, isBuild, isChannel, PLATFORMS, type Platform } from './versions.ts'

interface Options {
  builds: string[]
  force: boolean
  /** the --name=value ones */
  values: Map<string, string>
}

interface Step {
  about: string
  /** returns what failed: the first failure stops the rest (util.ts forEach) */
  run(options: Options): Promise<string[] | void> | string[] | void
}

const steps: Record<string, Step> = {
  track: {
    about:
      'what minecraft.net offers now, both channels (or the builds named, as their zips name them: 1.21.42.01; --channel=preview): each build new to versions/, its zips hashed (--force: again)',
    run: trackBuilds,
  },
  support: {
    about:
      '<build...> [--platform=linux|windows]: the platform of this machine (its OS) of tracked releases read and checked as extract would; marked supported or not in versions/release/<build>.json',
    run: supportBuilds,
  },
  extract: {
    about: 'each supported release bedrock-data has not got the data of (or the builds named): its data into bedrock-data (--force: again, where it has)',
    run: extractBuilds,
  },
  steve: {
    about:
      "<build...> [--port=19150] [--force]: the default skin of a real client of each release's version, which joins this machine once, into bedrock-data; --import=<dir> [build...]: each one's from <dir>/<build>/steve.json",
    run: steveBuilds,
  },
  native: { about: 'builds the native agent (xmake) for the OS of this machine', run: () => host().buildAgent() },
  check: {
    about: '<build...>: the bindings of the agent resolved against the server of each tracked release on this platform, without starting it (one build: every binding)',
    run: checkBindings,
  },
  status: { about: 'each build in versions/, the platforms it is supported on, and whether bedrock-data has its data', run: status },
}

function versionStore(): VersionStore {
  return new VersionStore(VERSIONS_DIR, SCHEMAS_DIR)
}

function checkNames(builds: string[]): void {
  const bad = builds.filter((b) => !isBuild(b))

  if (bad.length) {
    throw new Error(`not an exact build (major.minor.patch.build): ${bad.join(', ')}`)
  }
}

async function offered(): Promise<Target[]> {
  const targets = new Map<string, Target & { platforms: Platform[] }>()

  for (const link of await fetchLinks()) {
    const key = `${link.channel}/${link.build}`
    const target = targets.get(key) ?? { channel: link.channel, build: link.build, platforms: [] }
    target.platforms.push(link.platform)
    targets.set(key, target)
  }

  const todo = [...targets.values()]
  console.log(`minecraft.net offers: ${todo.map((t) => `${targetName(t)} (${t.platforms.join(', ')})`).join(', ')}`)
  return todo
}

async function trackBuilds({ builds, force, values }: Options): Promise<string[]> {
  const channel = values.get('channel') ?? 'release'

  if (!isChannel(channel)) {
    throw new Error(`--channel: ${CHANNELS.join(' or ')}, not ${channel}`)
  }

  checkNames(builds)
  const store = versionStore()
  const targets = builds.length ? builds.map((build) => ({ channel, build })) : await offered()
  const todo = targets.filter((t) => force || !isTracked(store, t))

  if (!todo.length) {
    console.log('  nothing new')
  }

  const failed = await forEach(todo, JOBS, targetName, (t) => track(store, t, force))

  if (store.writeIndex()) {
    console.log('  versions/versions.json written')
  }

  return failed
}

async function supportBuilds({ builds, values }: Options): Promise<string[]> {
  if (!builds.length) {
    throw new Error('support: name the releases, as their zips name them (1.26.52.3)')
  }

  checkNames(builds)
  const platform = values.get('platform') ?? hostPlatform()
  const platforms = PLATFORMS.filter((p) => p === platform)

  if (!platforms.length) {
    throw new Error(`--platform: ${PLATFORMS.join(' or ')}, not ${platform}`)
  }

  agentHost(platforms[0])
  const store = versionStore()
  const untracked = builds.filter((b) => !store.read('release', b))

  if (untracked.length) {
    throw new Error(`not tracked as releases: ${untracked.join(', ')} (pnpm track <build>)`)
  }

  const failed: string[] = []
  const errors = await forEach(
    builds,
    JOBS,
    (build) => `release/${build}`,
    async (build) => {
      const result = await support(store, build, platforms, CACHE_DIR)

      for (const f of result.failed) {
        failed.push(`release/${build} (${f.platform})`)
        console.error(`  release/${build}: ${f.platform} FAILED: ${f.error}`)
      }

      return result.supported.length ? `supported on ${result.supported.join(', ')}` : undefined
    },
  )
  return [...errors, ...failed]
}

async function extractBuilds({ builds, force }: Options): Promise<string[]> {
  checkNames(builds)
  const store = versionStore()
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  console.log(`bedrock-data: ${repo.root}`)
  let todo = (builds.length ? builds : store.supportedBuilds()).filter((build) => force || !isExtracted(repo, build))

  if (!builds.length) {
    todo = todo.filter((build) => {
      const version = store.read('release', build)
      const from = version && exportPlatform(version)

      if (!from || from === hostPlatform()) {
        return true
      }

      console.log(`  release/${build}: exported from ${from}: pnpm extract ${build} there`)
      return false
    })
  }

  if (!todo.length) {
    console.log('  nothing to extract')
  }

  const failed = await forEach(
    todo,
    JOBS,
    (build) => `release/${build}`,
    (build, signal) => extract(repo, store, build, force, CACHE_DIR, readBlocks, signal),
  )

  if (repo.writeVersions()) {
    console.log('  versions.json written')
  }

  return failed
}

// the port a client joins to have its skin kept
const STEVE_PORT = 19150

/** steve: each release's default skin, recorded from a client (one at a time: a person joins each) or imported */
async function steveBuilds({ builds, force, values }: Options): Promise<string[]> {
  checkNames(builds)
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  const dir = values.get('import')
  const port = Number(values.get('port') ?? STEVE_PORT)
  const todo = builds.length ? builds : dir ? repo.builds().filter((build) => importedSkin(dir, build)) : []

  if (!todo.length) {
    throw new Error('steve: name the releases (or --import=<dir>, which takes every one it has)')
  }

  const failed: string[] = []

  for (const build of todo) {
    try {
      if (!dir && !force && repo.readBuild(build)?.data.steve) {
        console.log(`  release/${build}: ${STEVE_FILE} already there`)
        continue
      }

      const skin = dir ? importedSkin(dir, build) : await recordedSkin(build, port)

      if (!skin) {
        throw new Error(`no ${join(dir ?? '', build, 'steve.json')}`)
      }

      checkSteve(skin)
      console.log(`  release/${build}: ${repo.writeSteve(build, skin) ? `${STEVE_FILE} written` : 'unchanged'}`)
    } catch (e) {
      failed.push(`release/${build}`)
      console.log(`  release/${build}: FAILED: ${errorMessage(e)}`)
    }
  }

  return failed
}

async function checkBindings({ builds, values }: Options): Promise<string[]> {
  if (!builds.length) {
    throw new Error('check: name the releases, as their zips name them (1.26.52.3)')
  }

  checkNames(builds)
  const h = agentHost(isPlatform(values.get('platform')))
  const store = versionStore()
  return forEach(
    builds,
    JOBS,
    (build) => `release/${build}`,
    async (build) => {
      const version = store.read('release', build)

      if (!version) {
        throw new Error(`not tracked: pnpm track ${build}`)
      }

      const server = join(workDir(CACHE_DIR, build, h.platform), 'server')
      await unpackServer(await trackedZip(version, h.platform, CACHE_DIR), server, h)
      const { ok, output } = h.check(join(server, h.serverExe), build)
      rmSync(server, { recursive: true, force: true })

      if (builds.length === 1) {
        console.log(output)
      }

      if (!ok) {
        throw new Error(
          output
            .split('\n')
            .filter((line) => line.startsWith('FAILED'))
            .join('; ') || output,
        )
      }

      return `${h.platform}: every binding resolves`
    },
  )
}

function isPlatform(value: string | undefined): Platform | undefined {
  if (value === undefined) {
    return hostPlatform()
  }

  const platform = PLATFORMS.find((p) => p === value)

  if (!platform) {
    throw new Error(`--platform: ${PLATFORMS.join(' or ')}, not ${value}`)
  }

  return platform
}

function status(): void {
  const store = versionStore()
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  function zip(json: ReturnType<VersionStore['read']>, platform: Platform): string {
    const binary = json?.binary[platform]
    return !binary ? '-' : binary.supported ? 'supported' : 'zip'
  }
  const rows = [['channel', 'build', 'version', 'linux', 'windows', 'bedrock-data']]

  for (const channel of CHANNELS) {
    for (const build of store.builds(channel)) {
      const json = store.read(channel, build)
      const isSupported = PLATFORMS.some((p) => json?.binary[p]?.supported)
      const data = channel === 'preview' ? '' : isExtracted(repo, build) ? 'extracted' : isSupported ? 'MISSING' : '-'
      rows.push([channel, build, json?.version ?? '?', zip(json, 'linux'), zip(json, 'windows'), data])
    }
  }

  printTable(rows)
}

function printTable(rows: string[][]): void {
  const widths = rows[0].map((_, i) => Math.max(...rows.map((row) => row[i].length)))

  for (const row of rows) {
    console.log(
      row
        .map((cell, i) => cell.padEnd(widths[i]))
        .join('  ')
        .trimEnd(),
    )
  }
}

function usage(): string {
  const width = Math.max(...Object.keys(steps).map((name) => name.length))
  return ['usage: pnpm <step> [builds...] [--force]', ...Object.entries(steps).map(([name, step]) => `  ${name.padEnd(width)}  ${step.about}`)].join('\n')
}

async function main(args: string[]): Promise<number> {
  const [name = '', ...rest] = args

  if (!Object.hasOwn(steps, name)) {
    console.error(usage())
    return 2
  }

  try {
    const options: Options = {
      builds: rest.filter((a) => !a.startsWith('--')),
      force: rest.includes('--force'),
      values: new Map(rest.filter((a) => /^--\w+=/.test(a)).map((a) => [a.slice(2, a.indexOf('=')), a.slice(a.indexOf('=') + 1)])),
    }
    const failed = (await steps[name].run(options)) ?? []

    if (!failed.length) {
      return 0
    }

    console.error(`failed: ${failed.join(', ')}`)
    return 1
  } catch (e) {
    console.error(errorMessage(e))
    return 2
  }
}

process.exitCode = await main(process.argv.slice(2))
