export type Channel = 'release' | 'preview'
export type Platform = 'linux' | 'windows'

export const CHANNELS: readonly Channel[] = ['release', 'preview']
export const PLATFORMS: readonly Platform[] = ['linux', 'windows']

export function isBuild(name: string): boolean {
  return /^\d+\.\d+\.\d+\.\d+$/.test(name)
}

export function isChannel(name: string): name is Channel {
  return (CHANNELS as readonly string[]).includes(name)
}

export function compareVersions(a: string, b: string): number {
  const pa = a.split('.').map(Number)
  const pb = b.split('.').map(Number)

  for (let i = 0; i < Math.max(pa.length, pb.length); i++) {
    const d = (pa[i] ?? 0) - (pb[i] ?? 0)

    if (d) {
      return d
    }
  }

  return 0
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
