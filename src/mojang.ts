import { isBuild, zipUrl, type Channel, type Platform } from './versions.ts'

export const DOWNLOAD_LINKS = 'https://net-secondary.web.minecraft-services.net/api/v1.0/download/links'

interface Link {
  channel: Channel
  platform: Platform
  build: string
}

const DOWNLOAD_TYPES: Record<string, { channel: Channel; platform: Platform }> = {
  serverBedrockLinux: { channel: 'release', platform: 'linux' },
  serverBedrockWindows: { channel: 'release', platform: 'windows' },
  serverBedrockPreviewLinux: { channel: 'preview', platform: 'linux' },
  serverBedrockPreviewWindows: { channel: 'preview', platform: 'windows' },
}

export function parseLinks(answer: unknown): Link[] {
  const links = (answer as { result?: { links?: unknown } } | undefined)?.result?.links

  if (!Array.isArray(links)) {
    throw new Error(`${DOWNLOAD_LINKS}: no result.links in ${JSON.stringify(answer).slice(0, 200)}`)
  }

  const out: Link[] = []

  for (const link of links as { downloadType?: unknown; downloadUrl?: unknown }[]) {
    const type = typeof link.downloadType === 'string' && Object.hasOwn(DOWNLOAD_TYPES, link.downloadType) ? DOWNLOAD_TYPES[link.downloadType] : undefined

    if (!type) {
      continue
    }

    const url = String(link.downloadUrl)
    const build = /\/bedrock-server-([\d.]+)\.zip$/.exec(url)?.[1] ?? ''

    if (!isBuild(build) || url !== zipUrl(build, type.channel, type.platform)) {
      throw new Error(`${DOWNLOAD_LINKS}: ${String(link.downloadType)} is ${url}, not a zip where versions.ts zipUrl looks for one`)
    }

    out.push({ ...type, build })
  }

  if (!out.some((l) => l.channel === 'release')) {
    throw new Error(`${DOWNLOAD_LINKS}: no release server among ${JSON.stringify(links).slice(0, 300)}`)
  }

  return out
}

export async function fetchLinks(): Promise<Link[]> {
  const res = await fetch(DOWNLOAD_LINKS, { headers: { accept: 'application/json' } })

  if (!res.ok) {
    throw new Error(`${DOWNLOAD_LINKS}: HTTP ${res.status}`)
  }

  return parseLinks(await res.json())
}
