// A client in a running server's world: what the server sends one of the items (the item registry, the creative list,
// the items' components), of the recipes, the biomes, the entities and the commands, and of its player's attributes;
// and a player its console commands can run as. bedrock-protocol joins and decodes as the build's version where it
// knows it, else as the newest it knows of the protocol the server advertises: a build joins once bedrock-protocol
// knows a version of its protocol. It joins over the server's transport: RakNet on its port, or NetherNet, which finds
// the server by its advertisement on the LAN.
import type { Client, ClientOptions } from 'bedrock-protocol'
import { createRequire } from 'node:module'
import {
  attributesOf,
  biomeDefinitionsOf,
  commandsOf,
  entityIdentifiersOf,
  itemComponentsOf,
  recipesOf,
  type Attribute,
  type BiomeDefinition,
  type Commands,
  type EntityIdentifier,
  type Recipe,
} from './packets.ts'
import type { RunningServer } from './server.ts'

const JOIN_TIMEOUT_MS = 120_000
// a ping's own timeout, and how many go unanswered before the join fails
const PING_TIMEOUT_MS = 5_000
const PING_TRIES = 12
// NetherNet's connection (WebRTC) once the server is found: more than bedrock-protocol's 9 s on a busy machine
const CONNECT_TIMEOUT_MS = 30_000
// from 1.26.43 the server silently refuses bedrock-protocol's default device (7, Win10); 8 (Win32) joins every build
const DEVICE_OS = 8

/** An entry of the item registry the server sends (start_game's itemstates, from 1.21.60 item_registry's) */
export interface ItemState {
  name: string
  runtime_id: number
  component_based: boolean
  /** from 1.21.60 */
  version?: string | number
  /** from 1.21.60, the components of a component-based item, as an NBT tag */
  nbt?: unknown
}

export interface CreativeStack {
  /** the item's runtime id */
  id: number
  metadata: number
  /** the block state it places: a runtime id, from 1.19.80 a state's network hash; 0 where it places none */
  block: number
  /** the stack's own NBT where it has some (an enchanted book's) */
  nbt?: unknown
}

export interface Sent {
  itemStates: ItemState[]
  creative: CreativeStack[]
  /** the joining player's */
  attributes: Attribute[]
  biomes: BiomeDefinition[]
  entities: EntityIdentifier[]
  recipes: Recipe[]
  commands: Commands
  /** item_component's, where the server sends it: each item's components as an NBT tag, by name */
  itemComponents?: Map<string, unknown>
}

// the packets kept besides the item registry and the creative list, the first of each the client receives (of
// update_attributes, the first for its player); item_component where the server sends one before they are all in
const KEPT = ['biome_definition_list', 'available_entity_identifiers', 'crafting_data', 'available_commands', 'update_attributes'] as const
type Kept = (typeof KEPT)[number] | 'item_component'

interface RawStack {
  network_id?: unknown
  metadata?: unknown
  block_runtime_id?: unknown
  extra?: { nbt?: { nbt?: unknown } }
}

function creativeStacks(params: { items?: unknown }): CreativeStack[] {
  if (!Array.isArray(params.items)) {
    throw new Error('creative_content has no items')
  }

  // an empty stack (network id 0) holds no item
  return (params.items as { item?: RawStack }[]).flatMap(({ item }, i) => {
    if (typeof item?.network_id !== 'number') {
      throw new Error(`creative item ${i} has no network id`)
    }

    if (item.network_id === 0) {
      return []
    }

    const nbt = item.extra?.nbt?.nbt
    return [
      {
        id: item.network_id,
        metadata: typeof item.metadata === 'number' ? item.metadata : 0,
        block: typeof item.block_runtime_id === 'number' ? item.block_runtime_id >>> 0 : 0,
        ...(nbt ? { nbt } : {}),
      },
    ]
  })
}

export interface Release {
  minecraftVersion: string
  /** its protocol */
  version: number
}

/** The releases bedrock-protocol knows, newest first */
export function knownReleases(): Release[] {
  const require = createRequire(import.meta.url)
  const mcData = createRequire(require.resolve('bedrock-protocol'))('minecraft-data') as { versions: { bedrock: (Release & { releaseType: string })[] } }
  return mcData.versions.bedrock.filter((v) => v.releaseType === 'release')
}

interface Advertised {
  protocol: number
  /** a NetherNet server's */
  networkId?: bigint
}

/** What `server` advertises: pinged until it answers */
async function advertised(server: RunningServer): Promise<Advertised> {
  const { ping } = await import('bedrock-protocol')

  for (let i = 0; ; i++) {
    try {
      const ad =
        server.transport === 'nethernet'
          ? await ping({ transport: 'nethernet', host: '127.0.0.1', timeout: PING_TIMEOUT_MS })
          : await ping({ transport: 'raknet', host: '127.0.0.1', port: server.port, timeout: PING_TIMEOUT_MS })
      // the advertisement holds it as text
      return { protocol: Number(ad.protocol), ...('networkId' in ad && ad.networkId != null ? { networkId: BigInt(ad.networkId) } : {}) }
    } catch (e) {
      if (i + 1 >= PING_TRIES) {
        throw new Error(`the client: the server answers no ping (${e instanceof Error ? e.message : String(e)})`, { cause: e })
      }
    }
  }
}

/** The options bedrock-protocol joins `build`'s server with: the build's version where known, else the newest known of the protocol the server advertises */
async function joinOptions(build: string, server: RunningServer): Promise<ClientOptions> {
  const releases = knownReleases()
  const own = build.split('.').slice(0, 3).join('.')
  const isKnown = releases.some((r) => r.minecraftVersion === own)
  const ad = !isKnown || server.transport === 'nethernet' ? await advertised(server) : undefined
  const version = isKnown ? own : releases.find((r) => r.version === ad?.protocol)?.minecraftVersion

  if (!version) {
    throw new Error(`the client: bedrock-protocol knows no version of protocol ${ad?.protocol} (${build})`)
  }

  const common = { host: '127.0.0.1', version: version as ClientOptions['version'], username: 'bdg', offline: true, deviceOS: DEVICE_OS, conLog: () => {} }

  if (server.transport === 'nethernet') {
    return { ...common, transport: 'nethernet', nethernet: { networkId: ad?.networkId }, skipPing: true, connectTimeout: CONNECT_TIMEOUT_MS }
  }

  return { ...common, transport: 'raknet', port: server.port, raknetBackend: 'raknet-native' }
}

/** Joins `build`'s `server`, keeps what it sends (`Sent`), and runs `whileIn` once the client is in its world. */
export async function joinServer(build: string, server: RunningServer, whileIn: () => Promise<void>): Promise<Sent> {
  const { createClient } = await import('bedrock-protocol')
  const client: Client = createClient(await joinOptions(build, server))
  let itemStates: ItemState[] | undefined
  let creative: CreativeStack[] | undefined
  let player: string | undefined
  const kept = new Map<Kept, Record<string, unknown>>()
  // a packet bedrock-protocol cannot decode is an error event; only those the export needs fail it
  let failed: Error | undefined
  client.on('error', (e: unknown) => {
    failed = e instanceof Error ? e : new Error(String(e))
  })
  client.on('start_game', (params: { itemstates?: ItemState[]; runtime_entity_id?: bigint | number }) => {
    player = String(params.runtime_entity_id)

    if (params.itemstates?.length) {
      itemStates = params.itemstates
    }
  })

  for (const name of [...KEPT, 'item_component'] as const) {
    client.on(name, (params: Record<string, unknown>) => {
      const forPlayer = name !== 'update_attributes' || String(params.runtime_entity_id as bigint | number) === player

      if (forPlayer && !kept.has(name)) {
        kept.set(name, params)
      }
    })
  }

  client.on('item_registry', (params: { itemstates?: ItemState[] }) => {
    itemStates = params.itemstates
  })
  client.on('creative_content', (params: { items?: unknown }) => {
    try {
      creative = creativeStacks(params)
    } catch (e) {
      failed = e instanceof Error ? e : new Error(String(e))
    }
  })

  try {
    const deadline = Date.now() + JOIN_TIMEOUT_MS

    while (!itemStates || !creative || KEPT.some((name) => !kept.has(name))) {
      if (Date.now() > deadline) {
        const missing = [...(itemStates ? [] : ['the item registry']), ...(creative ? [] : ['creative_content']), ...KEPT.filter((name) => !kept.has(name))]
        throw new Error(`the client: no ${missing.join(', ')} within ${JOIN_TIMEOUT_MS / 1000} s${failed ? ` (${failed.message.split('\n')[0]})` : ''}`)
      }

      await new Promise((resolve) => setTimeout(resolve, 200))
    }

    await whileIn()

    function packet(name: Kept): Record<string, unknown> {
      return kept.get(name) ?? {}
    }

    const itemComponents = kept.get('item_component')
    return {
      itemStates,
      creative,
      attributes: attributesOf(packet('update_attributes')),
      biomes: biomeDefinitionsOf(packet('biome_definition_list')),
      entities: entityIdentifiersOf(packet('available_entity_identifiers')),
      recipes: recipesOf(packet('crafting_data'), new Map(itemStates.map((s) => [s.runtime_id, s.name]))),
      commands: commandsOf(packet('available_commands')),
      ...(itemComponents ? { itemComponents: itemComponentsOf(itemComponents) } : {}),
    }
  } finally {
    client.close()
  }
}
