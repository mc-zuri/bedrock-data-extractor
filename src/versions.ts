export type Channel = 'release' | 'preview'
export type Platform = 'linux' | 'windows'

export const CHANNELS: readonly Channel[] = ['release', 'preview']
export const PLATFORMS: readonly Platform[] = ['linux', 'windows']

/** A build of a channel: what versions/ tracks, and bedrock-data keeps at `<channel>/<build>` */
export interface Target {
  channel: Channel
  build: string
}

export function targetName(t: Target): string {
  return `${t.channel}/${t.build}`
}

/** The command that tracks `t` */
export function trackCommand(t: Target): string {
  return `pnpm track ${t.build}${t.channel === 'preview' ? ' --channel=preview' : ''}`
}

export function isBuild(name: string): boolean {
  return /^\d+\.\d+\.\d+\.\d+$/.test(name)
}

export function isChannel(name: string): name is Channel {
  return (CHANNELS as readonly string[]).includes(name)
}

// a build's fourth number from which it is a preview
const PREVIEW_FROM = 20

// where a build sorts among its version's: the version alone first, then its previews, then its releases
function rankInVersion(number: number | undefined): number {
  return number === undefined ? 0 : number >= PREVIEW_FROM ? 1 : 2
}

/**
 * Bedrock's order: by the version (the first three numbers), then that version's previews (the fourth number from 20),
 * then its releases (below 20), which come out after them. A version alone ("1.26.20") is where its builds begin.
 */
export function compareVersions(a: string, b: string): number {
  const pa = a.split('.').map(Number)
  const pb = b.split('.').map(Number)

  for (let i = 0; i < 3; i++) {
    const d = (pa[i] ?? 0) - (pb[i] ?? 0)

    if (d) {
      return d
    }
  }

  return rankInVersion(pa[3]) - rankInVersion(pb[3]) || (pa[3] ?? 0) - (pb[3] ?? 0)
}

export function versionOf(build: string, channel: Channel): string {
  const [major, minor, patch, number] = build.split('.')
  const version = `${major}.${minor}.${patch}`
  return channel === 'release' ? version : `${version}-preview.${Number(number)}`
}

const ZIP_FOLDERS: Record<Channel, Record<Platform, string>> = {
  release: { linux: 'bin-linux', windows: 'bin-win' },
  preview: { linux: 'bin-linux-preview', windows: 'bin-win-preview' },
}

export function zipUrl(build: string, channel: Channel, platform: Platform): string {
  return `https://www.minecraft.net/bedrockdedicatedserver/${ZIP_FOLDERS[channel][platform]}/bedrock-server-${build}.zip`
}
