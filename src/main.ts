import { BEDROCK_DATA_DIR, CACHE_DIR, JOBS, SCHEMAS_DIR, VERSIONS_DIR } from './config.ts'
import { DataRepo } from './dataRepo.ts'
import { extract, isExtracted } from './extract.ts'
import { fetchLinks } from './mojang.ts'
import { isTracked, targetName, track, type Target } from './track.ts'
import { errorMessage, forEach } from './util.ts'
import { VersionStore } from './versionStore.ts'
import { CHANNELS, isBuild, isChannel, type Platform } from './versions.ts'

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
  extract: {
    about: 'each release in versions/ that bedrock-data has not got as versions/ has it now (or the builds named): its data into bedrock-data (--force: again)',
    run: extractBuilds,
  },
  status: { about: 'each build in versions/, and whether bedrock-data has its data', run: status },
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

async function extractBuilds({ builds, force }: Options): Promise<string[]> {
  checkBuilds(builds)
  const store = versionStore()
  const repo = new DataRepo(BEDROCK_DATA_DIR)
  console.log(`bedrock-data: ${repo.root}`)
  function pending(build: string): boolean {
    const version = store.read('release', build)
    return force || version === undefined || !isExtracted(repo, version)
  }
  const todo = builds.length ? builds : store.builds('release').filter(pending)
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
  const rows = [['channel', 'build', 'version', 'linux', 'windows', 'bedrock-data']]
  for (const channel of CHANNELS)
    for (const build of store.builds(channel)) {
      const json = store.read(channel, build)
      const data = channel === 'preview' ? '' : !json ? '?' : isExtracted(repo, json) ? 'extracted' : repo.readBuild(build) ? 'OUT OF DATE' : 'MISSING'
      rows.push([channel, build, json?.version ?? '?', json?.binary.linux ? 'yes' : '-', json?.binary.windows ? 'yes' : '-', data])
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
