// A build as minecraft.net names its zip (bedrock-server-1.21.42.01.zip: 1.21.42.01, never 1.21.42.1), its channel and
// where each platform's zip of it is.

export type Channel = 'release' | 'preview'
export type Platform = 'linux' | 'windows'

export const CHANNELS: readonly Channel[] = ['release', 'preview']
/** linux first: it is the platform a build's data is read from where both have it */
export const PLATFORMS: readonly Platform[] = ['linux', 'windows']

export function isBuild(name: string): boolean {
  return /^\d+\.\d+\.\d+\.\d+$/.test(name)
}

export function isChannel(name: string): name is Channel {
  return (CHANNELS as readonly string[]).includes(name)
}

/** Part by part as numbers: missing parts count as 0, so 1.21.0 sorts before every 1.21.0.x build; 01 and 1 are equal. */
export function compareVersions(a: string, b: string): number {
  const pa = a.split('.').map(Number)
  const pb = b.split('.').map(Number)
  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? 0) - (pb[i] ?? 0)
    if (d) return d
  }
  return 0
}

/** A release's major.minor.patch (1.26.52); a preview's, with its build as the preview's number (1.26.70-preview.20). */
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
