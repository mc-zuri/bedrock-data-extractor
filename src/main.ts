import { BEDROCK_DATA_DIR, JOBS } from './config.ts'
import { DataRepo } from './dataRepo.ts'
import { extract, isComplete, targetName, type Target } from './extract.ts'
import { fetchLinks } from './mojang.ts'
import { errorMessage, forEach } from './util.ts'
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
  run(repo: DataRepo, options: Options): Promise<string[] | void> | string[] | void
}

const steps: Record<string, Step> = {
  track: { about: 'what minecraft.net offers now, both channels: each build that is new here, or kept without all it should have, downloaded and extracted', run: track },
  extract: { about: '<build...> [--channel=preview]: the given builds (as their zips name them: 1.21.42.01), again with --force', run: extractBuilds },
  status: { about: 'each build kept, its downloads and its data', run: status },
}

async function runAll(repo: DataRepo, todo: Target[], force: boolean): Promise<string[]> {
  console.log(`bedrock-data: ${repo.root}`)
  const failed = await forEach(todo, JOBS, targetName, (t) => extract(repo, t, force))
  if (repo.writeVersions()) console.log('  versions.json written')
  return failed
}

async function track(repo: DataRepo, { force }: Options): Promise<string[]> {
  const targets = new Map<string, Target & { platforms: Platform[] }>()
  for (const link of await fetchLinks()) {
    const key = `${link.channel}/${link.build}`
    const target = targets.get(key) ?? { channel: link.channel, build: link.build, platforms: [] }
    target.platforms.push(link.platform)
    targets.set(key, target)
  }
  const todo = [...targets.values()]
  console.log(`minecraft.net offers: ${todo.map((t) => `${targetName(t)} (${t.platforms.join(', ')})`).join(', ')}`)
  const fresh = todo.filter((t) => force || !isComplete(repo, t))
  if (!fresh.length) console.log('  nothing new')
  return runAll(repo, fresh, force)
}

async function extractBuilds(repo: DataRepo, { builds, force, values }: Options): Promise<string[]> {
  const channel = values.get('channel') ?? 'release'
  if (!isChannel(channel)) throw new Error(`--channel: ${CHANNELS.join(' or ')}, not ${channel}`)
  if (!builds.length) throw new Error('extract: name the builds, as their zips name them (1.21.42.01)')
  const bad = builds.filter((b) => !isBuild(b))
  if (bad.length) throw new Error(`not an exact build (major.minor.patch.build): ${bad.join(', ')}`)
  return runAll(
    repo,
    builds.map((build) => ({ channel, build })),
    force,
  )
}

function status(repo: DataRepo): void {
  const rows = [['channel', 'build', 'version', 'linux', 'windows', 'language']]
  for (const channel of CHANNELS)
    for (const build of repo.builds(channel)) {
      const json = repo.readBuild(channel, build)
      rows.push([
        channel,
        build,
        json?.version ?? '?',
        json?.binary.linux ? 'yes' : '-',
        json?.binary.windows ? 'yes' : '-',
        channel === 'preview' ? '' : json?.data?.language && repo.hasLanguage(build) ? `from ${json.data.language.from}` : 'MISSING',
      ])
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
    const failed = (await steps[name].run(new DataRepo(BEDROCK_DATA_DIR), options)) ?? []
    if (!failed.length) return 0
    console.error(`failed: ${failed.join(', ')}`)
    return 1
  } catch (e) {
    console.error(errorMessage(e))
    return 2
  }
}

process.exitCode = await main(process.argv.slice(2))
