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
import { isTracked, track, type Offer } from './track.ts'
import { errorMessage, forEach } from './util.ts'
import { VersionStore } from './versionStore.ts'
import { CHANNELS, isBuild, isChannel, PLATFORMS, targetName, type Channel, type Platform, type Target } from './versions.ts'

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
      '<build...> [--platform=linux|windows] [--channel=release|preview]: the platform of this machine (its OS) of tracked builds read and checked as extract would; marked supported or not in versions/<channel>/<build>.json',
    run: supportBuilds,
  },
  extract: {
    about: 'each supported build bedrock-data has not got the data of (or the builds named): its data into bedrock-data (--force: again, where it has)',
    run: extractBuilds,
  },
  steve: {
    about:
      "<build...> [--port=19150] [--force]: the default skin of a real client of each release's version, which joins this machine once, into bedrock-data; --import=<dir> [build...]: each one's from <dir>/<build>/steve.json",
    run: steveBuilds,
  },
  native: { about: 'builds the native agent (xmake) for the OS of this machine', run: () => host().buildAgent() },
  check: {
    about: '<build...>: the bindings of the agent resolved against the server of each tracked build on this platform, without starting it (one build: every binding)',
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

/** The builds named, each of the channel --channel names, else of the one that tracks it */
function targets(store: VersionStore, builds: string[], values: Map<string, string>): Target[] {
  checkNames(builds)
  return builds.map((build) => store.target(build, channelOption(values)))
}

function channelOption(values: Map<string, string>): Channel | undefined {
  const channel = values.get('channel')

  if (channel !== undefined && !isChannel(channel)) {
    throw new Error(`--channel: ${CHANNELS.join(' or ')}, not ${channel}`)
  }

  return channel
}

async function offered(): Promise<Offer[]> {
  const targets = new Map<string, Offer & { platforms: Platform[] }>()

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
  const channel = channelOption(values) ?? 'release'
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
    throw new Error('support: name the builds, as their zips name them (1.26.52.3)')
  }

  const platform = values.get('platform') ?? hostPlatform()
  const platforms = PLATFORMS.filter((p) => p === platform)

  if (!platforms.length) {
    throw new Error(`--platform: ${PLATFORMS.join(' or ')}, not ${platform}`)
  }

  agentHost(platforms[0])
  const store = versionStore()
  const todo = targets(store, builds, values)
  const failed: string[] = []
  const errors = await forEach(todo, JOBS, targetName, async (t) => {
    const result = await support(store, t, platforms, CACHE_DIR)

    for (const f of result.failed) {
      failed.push(`${targetName(t)} (${f.platform})`)
      console.error(`  ${targetName(t)}: ${f.platform} FAILED: ${f.error}`)
    }

    return result.supported.length ? `supported on ${result.supported.join(', ')}` : undefined
  })
  return [...errors, ...failed]
}

async function extractBuilds({ builds, force, values }: Options): Promise<string[]> {
  const store = versionStore()
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  console.log(`bedrock-data: ${repo.root}`)
  let todo = (builds.length ? targets(store, builds, values) : store.supportedBuilds()).filter((t) => force || !isExtracted(repo, t))

  if (!builds.length) {
    todo = todo.filter((t) => {
      const version = store.read(t.channel, t.build)
      const from = version && exportPlatform(version)

      if (!from || from === hostPlatform()) {
        return true
      }

      console.log(`  ${targetName(t)}: exported from ${from}: pnpm extract ${t.build} there`)
      return false
    })
  }

  if (!todo.length) {
    console.log('  nothing to extract')
  }

  const failed = await forEach(todo, JOBS, targetName, (t, signal) => extract(repo, store, t, force, CACHE_DIR, readBlocks, signal))

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
  const todo = builds.length ? builds : dir ? repo.builds('release').filter((build) => importedSkin(dir, build)) : []

  if (!todo.length) {
    throw new Error('steve: name the releases (or --import=<dir>, which takes every one it has)')
  }

  const failed: string[] = []

  for (const build of todo) {
    const release: Target = { channel: 'release', build }

    try {
      if (!dir && !force && repo.readBuild(release)?.data.steve) {
        console.log(`  release/${build}: ${STEVE_FILE} already there`)
        continue
      }

      const skin = dir ? importedSkin(dir, build) : await recordedSkin(build, port)

      if (!skin) {
        throw new Error(`no ${join(dir ?? '', build, 'steve.json')}`)
      }

      checkSteve(skin)
      console.log(`  release/${build}: ${repo.writeSteve(release, skin) ? `${STEVE_FILE} written` : 'unchanged'}`)
    } catch (e) {
      failed.push(`release/${build}`)
      console.log(`  release/${build}: FAILED: ${errorMessage(e)}`)
    }
  }

  return failed
}

async function checkBindings({ builds, values }: Options): Promise<string[]> {
  if (!builds.length) {
    throw new Error('check: name the builds, as their zips name them (1.26.52.3)')
  }

  const h = agentHost(isPlatform(values.get('platform')))
  const store = versionStore()
  return forEach(targets(store, builds, values), JOBS, targetName, async (t) => {
    const version = store.read(t.channel, t.build)

    if (!version) {
      throw new Error(`not tracked: pnpm track ${t.build}`)
    }

    const server = join(workDir(CACHE_DIR, t, h.platform), 'server')
    await unpackServer(await trackedZip(version, h.platform, CACHE_DIR), server, h)
    const { ok, output } = h.check(join(server, h.serverExe), t.build)
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
  })
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
      const data = isExtracted(repo, { channel, build }) ? 'extracted' : isSupported ? 'MISSING' : '-'
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
