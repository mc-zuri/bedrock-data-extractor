// A client in a running server's world: what the server sends one of the items (the item registry, the creative list,
// the items' components), of the recipes, the biomes, the entities and the commands, and of its player's attributes;
// and a player its console commands can run as. bedrock-protocol joins and decodes as the build's version where it
// knows it (a release's), else as the newest it knows of the protocol the server advertises; where it knows none (a
// preview, a release newer than it), it decodes as the newest version it knows before that protocol, changed by each
// protocol since as far as src/protocol.ts knows them, and joins as the protocol the server takes; each packet the
// export reads must decode to its last byte. It joins over the server's
// transport: RakNet on its port, or NetherNet, which finds the server by its advertisement on the LAN.
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
import { useProtocol } from './protocol.ts'
import type { RunningServer } from './server.ts'
import { targetName, type Target } from './versions.ts'

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
// every packet the export reads
const READ: readonly string[] = ['start_game', 'item_registry', 'creative_content', 'item_component', ...KEPT]

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

/** A release's own version, where bedrock-protocol knows it: a preview's protocol is not its release's */
export function ownVersion(t: Target, releases: Release[]): string | undefined {
  const own = t.build.split('.').slice(0, 3).join('.')
  return t.channel === 'release' && releases.some((r) => r.minecraftVersion === own) ? own : undefined
}

/** The version bedrock-protocol decodes as; where the server's protocol is not the version's own, both protocols */
export interface JoinAs {
  version: string
  protocol?: number
  /** the version's own protocol */
  from?: number
}

/** For a server of `protocol`: the newest version known of it, else the newest known before it, joining as `protocol` */
export function joinAs(t: Target, protocol: number, releases: Release[]): JoinAs {
  const same = releases.find((r) => r.version === protocol)

  if (same) {
    return { version: same.minecraftVersion }
  }

  const before = releases.filter((r) => r.version < protocol).sort((a, b) => b.version - a.version)[0]

  if (!before) {
    throw new Error(`the client: bedrock-protocol knows no version of protocol ${protocol} or before (${targetName(t)})`)
  }

  return { version: before.minecraftVersion, protocol, from: before.version }
}

/** The options bedrock-protocol joins `t`'s server with; where it knows none of the server's protocol, both protocols */
async function joinOptions(t: Target, server: RunningServer): Promise<{ options: ClientOptions; protocol?: number; from?: number }> {
  const releases = knownReleases()
  const own = ownVersion(t, releases)
  const ad = !own || server.transport === 'nethernet' ? await advertised(server) : undefined
  const { version, protocol, from } = own ? { version: own } : joinAs(t, (ad as Advertised).protocol, releases)
  const common = { host: '127.0.0.1', version: version as ClientOptions['version'], username: 'bdg', offline: true, deviceOS: DEVICE_OS, conLog: () => {} }

  if (server.transport === 'nethernet') {
    return { options: { ...common, transport: 'nethernet', nethernet: { networkId: ad?.networkId }, skipPing: true, connectTimeout: CONNECT_TIMEOUT_MS }, protocol, from }
  }

  return { options: { ...common, transport: 'raknet', port: server.port, raknetBackend: 'raknet-native' }, protocol, from }
}

/** A packet as bedrock-protocol decoded it */
interface Decoded {
  data: { name: string }
  metadata: { size: number }
  fullBuffer: Buffer
}

/** Joins `t`'s `server`, keeps what it sends (`Sent`), and runs `whileIn` once the client is in its world. */
export async function joinServer(t: Target, server: RunningServer, whileIn: () => Promise<void>): Promise<Sent> {
  const { createClient } = await import('bedrock-protocol')
  const { options, protocol, from } = await joinOptions(t, server)
  const client: Client = createClient(options)
  // a packet the export reads, decoded as an earlier version and read short: it changed since
  let short: string | undefined

  if (protocol !== undefined) {
    // bedrock-protocol sets it from the version; the server takes only its own, which the client sends once connected
    const settings = (client as unknown as { options: { protocolVersion: number } }).options
    settings.protocolVersion = protocol
    useProtocol(client, options.version as string, from as number, protocol)
    client.on('packet', (packet: Decoded) => {
      const { name } = packet.data

      if (READ.includes(name) && packet.metadata.size !== packet.fullBuffer.length) {
        short ??= `${name} decodes ${packet.metadata.size} of its ${packet.fullBuffer.length} bytes as ${options.version}`
      }
    })
  }

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

    // item_component may come after the others
    if (short) {
      throw new Error(`the client: ${short} (protocol ${protocol})`)
    }

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
