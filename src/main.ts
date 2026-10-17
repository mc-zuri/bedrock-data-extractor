import { BEDROCK_DATA_DIR, CACHE_DIR, JOBS, SCHEMAS_DIR, VERSIONS_DIR } from './config.ts'
import { DataRepo } from './dataRepo.ts'
import { extract, isExtracted } from './extract.ts'
import { fetchLinks } from './mojang.ts'
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
  /** returns what failed, where some may fail and the others go on */
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
      '<build...> [--platform=linux|windows]: each platform of tracked releases read and checked as extract would; those that pass marked supported in versions/release/<build>.json',
    run: supportBuilds,
  },
  extract: {
    about: 'each supported release bedrock-data has not got the data of (or the builds named): its data into bedrock-data (--force: again, where it has)',
    run: extractBuilds,
  },
  status: { about: 'each build in versions/, the platforms it is supported on, and whether bedrock-data has its data', run: status },
}

function versionStore(): VersionStore {
  return new VersionStore(VERSIONS_DIR, SCHEMAS_DIR)
}

function checkBuilds(builds: string[]): void {
  const bad = builds.filter((b) => !isBuild(b))
  if (bad.length) throw new Error(`not an exact build (major.minor.patch.build): ${bad.join(', ')}`)
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
  if (!isChannel(channel)) throw new Error(`--channel: ${CHANNELS.join(' or ')}, not ${channel}`)
  checkBuilds(builds)
  const store = versionStore()
  const targets = builds.length ? builds.map((build) => ({ channel, build })) : await offered()
  const todo = targets.filter((t) => force || !isTracked(store, t))
  if (!todo.length) console.log('  nothing new')
  const failed = await forEach(todo, JOBS, targetName, (t) => track(store, t, force))
  if (store.writeIndex()) console.log('  versions/versions.json written')
  return failed
}

/** Returns each release and platform that failed, as release/<build> (<platform>). */
async function supportBuilds({ builds, values }: Options): Promise<string[]> {
  if (!builds.length) throw new Error('support: name the releases, as their zips name them (1.26.52.3)')
  checkBuilds(builds)
  const platform = values.get('platform')
  const platforms = platform === undefined ? PLATFORMS : PLATFORMS.filter((p) => p === platform)
  if (!platforms.length) throw new Error(`--platform: ${PLATFORMS.join(' or ')}, not ${platform}`)
  const store = versionStore()
  const untracked = builds.filter((b) => !store.read('release', b))
  if (untracked.length) throw new Error(`not tracked as releases: ${untracked.join(', ')} (pnpm track <build>)`)
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
  checkBuilds(builds)
  const store = versionStore()
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  console.log(`bedrock-data: ${repo.root}`)
  const todo = (builds.length ? builds : store.supportedBuilds()).filter((build) => force || !isExtracted(repo, build))
  if (!todo.length) console.log('  nothing to extract')
  const failed = await forEach(
    todo,
    JOBS,
    (build) => `release/${build}`,
    (build) => extract(repo, store, build, force, CACHE_DIR),
  )
  if (repo.writeVersions()) console.log('  versions.json written')
  return failed
}

function status(): void {
  const store = versionStore()
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  // each platform: supported, a zip that is not (yet), or none
  function zip(json: ReturnType<VersionStore['read']>, platform: Platform): string {
    const binary = json?.binary[platform]
    return !binary ? '-' : binary.supported ? 'supported' : 'zip'
  }
  const rows = [['channel', 'build', 'version', 'linux', 'windows', 'bedrock-data']]
  for (const channel of CHANNELS)
    for (const build of store.builds(channel)) {
      const json = store.read(channel, build)
      const isSupported = PLATFORMS.some((p) => json?.binary[p]?.supported)
      const data = channel === 'preview' ? '' : isExtracted(repo, build) ? 'extracted' : isSupported ? 'MISSING' : '-'
      rows.push([channel, build, json?.version ?? '?', zip(json, 'linux'), zip(json, 'windows'), data])
    }
  printTable(rows)
}

function printTable(rows: string[][]): void {
  const widths = rows[0].map((_, i) => Math.max(...rows.map((row) => row[i].length)))
  for (const row of rows)
    console.log(
      row
        .map((cell, i) => cell.padEnd(widths[i]))
        .join('  ')
        .trimEnd(),
    )
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
    if (!failed.length) return 0
    console.error(`failed: ${failed.join(', ')}`)
    return 1
  } catch (e) {
    console.error(errorMessage(e))
    return 2
  }
}

process.exitCode = await main(process.argv.slice(2))
