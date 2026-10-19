// The skin a Minecraft client sends in its login, the default one (Steve), which bedrock-data keeps per release. No
// server holds it: `pnpm steve` listens as a server of the release's version (bedrock-protocol's: the skin is in the
// first packet a client sends, so no BDS runs) until a real client of that version joins it once, and keeps the skin;
// or it takes earlier captures (--import=<dir>: <dir>/<build>/steve.json).
// The client tags the skin with a persona id (persona-<PlayFab id>-<n>) in SkinId, SkinResourcePatch and
// SkinGeometryData, which names the capturing account and counts captures: it is replaced, in all three, by a fixed one,
// so captures of the same skin are equal and no account is in them.
import type { ServerOptions } from 'bedrock-protocol'
import { existsSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { knownReleases } from './client.ts'

export const STEVE_FILE = 'steve.json.gz'

export type Skin = Record<string, unknown>

// the skin of a login, as bedrock-protocol names its fields, in this order (SkinGeometryDataEngineVersion from 1.17.30)
const SKIN_KEYS = [
  'AnimatedImageData',
  'ArmSize',
  'CapeData',
  'CapeId',
  'CapeImageHeight',
  'CapeImageWidth',
  'CapeOnClassicSkin',
  'PersonaPieces',
  'PersonaSkin',
  'PieceTintColors',
  'PremiumSkin',
  'SkinAnimationData',
  'SkinColor',
  'SkinData',
  'SkinGeometryData',
  'SkinGeometryDataEngineVersion',
  'SkinId',
  'SkinImageHeight',
  'SkinImageWidth',
  'SkinResourcePatch',
]

// older clients write the PlayFab id in hex, newer ones in base 36 (persona-437vsr5lh19kek5q-3)
const CANONICAL_PLAYFAB_ID = '123456789012345'

function withCanonicalPersona(text: string): string {
  return text.replace(/(persona[-_])[0-9a-z]+-\d+/gi, (_match, prefix: string) => `${prefix}${CANONICAL_PLAYFAB_ID}-0`)
}

/** The skin fields of a login, its persona ids made the fixed one. */
export function normalizedSkin(login: Skin): Skin {
  const skin: Skin = Object.fromEntries(SKIN_KEYS.filter((k) => k in login).map((k) => [k, login[k]]))

  if (typeof skin.SkinId === 'string') {
    skin.SkinId = withCanonicalPersona(skin.SkinId)
  }

  for (const key of ['SkinResourcePatch', 'SkinGeometryData']) {
    const value = skin[key]

    if (typeof value === 'string') {
      skin[key] = Buffer.from(withCanonicalPersona(Buffer.from(value, 'base64').toString('utf8')), 'utf8').toString('base64')
    }
  }

  return skin
}

/** An earlier capture of `build`'s, `<dir>/<build>/steve.json`; undefined where there is none */
export function importedSkin(dir: string, build: string): Skin | undefined {
  const file = join(dir, build, 'steve.json')
  return existsSync(file) ? normalizedSkin(JSON.parse(readFileSync(file, 'utf8')) as Skin) : undefined
}

// how long a person has to join
const WAIT_MS = 600_000

interface SkinPlayer {
  skinData?: Skin
  on(event: 'login', listener: () => void): void
  disconnect(reason: string): void
}

interface SkinServer {
  on(event: 'connect', listener: (player: SkinPlayer) => void): void
  on(event: 'error', listener: (e: unknown) => void): void
  close(): Promise<void> | void
}

/** The skin of the first client that logs in to a server of `build`'s version listening on `port`. */
export async function recordedSkin(build: string, port: number): Promise<Skin> {
  const own = build.split('.').slice(0, 3).join('.')

  if (!knownReleases().some((r) => r.minecraftVersion === own)) {
    throw new Error(`bedrock-protocol knows no version ${own}: update it first`)
  }

  const { createServer } = await import('bedrock-protocol')
  const options = { host: '0.0.0.0', port, version: own, offline: true } as ServerOptions
  const server = createServer(options) as unknown as SkinServer
  console.log(`  release/${build}: in Minecraft ${own}, join this machine on port ${port} once, with the default skin`)

  try {
    return await new Promise<Skin>((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`no client joined within ${WAIT_MS / 1000} s`)), WAIT_MS)
      server.on('connect', (player) => {
        player.on('login', () => {
          if (player.skinData) {
            clearTimeout(timer)
            resolve(normalizedSkin(player.skinData))
            player.disconnect('Thanks: your skin is kept')
          }
        })
      })
      server.on('error', (e) => {
        clearTimeout(timer)
        reject(e instanceof Error ? e : new Error(String(e)))
      })
    })
  } finally {
    await server.close()
  }
}
