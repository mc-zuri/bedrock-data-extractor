import assert from 'node:assert/strict'
import { createRequire } from 'node:module'
import { test } from 'node:test'
import { changedTypes, useProtocol, type Change } from '../src/protocol.ts'

type Types = Parameters<typeof changedTypes>[0]

function types(version: string): Types {
  const require = createRequire(createRequire(import.meta.url).resolve('bedrock-protocol'))
  return (require('minecraft-data') as (v: string) => { protocol: { types: Types } })(`bedrock_${version}`).protocol.types
}

function fieldNames(type: unknown): string[] {
  return ((type as [string, { name?: string }[]])[1] ?? []).map((f) => f.name ?? '(anon)')
}

interface Proto {
  createPacketBuffer(type: string, packet: unknown): Buffer
  parsePacketBuffer(type: string, buffer: Buffer): { data: { params: unknown }; metadata: { size: number } }
}

function startGame(t: Types): string[] {
  return fieldNames(t.packet_start_game)
}

function animateChange(c: Partial<Change>): Change[] {
  return [{ protocol: 2200, type: 'packet_animate', ...c }]
}

function protocolOf(to: number): Proto {
  const client = { serializer: { proto: undefined }, deserializer: { proto: undefined } }
  useProtocol(client as never, '1.26.51', 2193, to)
  return client.serializer.proto as unknown as Proto
}

void test("a protocol's changes apply after the version's own protocol, up to the server's", () => {
  const before = changedTypes(types('1.26.51'), 2193, 2211)
  const after = changedTypes(types('1.26.51'), 2193, 2216)
  assert.ok(!startGame(before).includes('editor_level_migration_version'))
  assert.deepEqual(startGame(after).slice(startGame(after).indexOf('exported_from_editor'), startGame(after).indexOf('exported_from_editor') + 3), [
    'exported_from_editor',
    'editor_level_migration_version',
    'day_cycle_stop_time',
  ])
  assert.ok(!fieldNames(after.Skin).includes('play_fab_id'))
  assert.deepEqual(changedTypes(types('1.26.51'), 2193, 2193), types('1.26.51'))
})

void test('a change the definition has no place for fails', () => {
  assert.throws(() => changedTypes(types('1.26.51'), 2193, 2207, animateChange({ drop: 'nothing' })), /packet_animate has no nothing to drop for 2200/)
  assert.throws(() => changedTypes(types('1.26.51'), 2193, 2207, animateChange({ after: 'nothing', add: { name: 'x', type: 'bool' } })), /no nothing to add x after/)
  assert.throws(() => changedTypes(types('1.26.51'), 2193, 2207, animateChange({ path: ['data'], add: { name: 'x', type: 'bool' } })), /packet_animate\.data is no container/)
  assert.throws(() => changedTypes(types('1.26.51'), 2193, 2207, animateChange({ type: 'packet_nothing', drop: 'x' })), /no packet_nothing to change/)
})

void test("a preview's packets read whole and write back as they were", () => {
  const proto = protocolOf(2238)
  const packets = [
    {
      name: 'level_chunk',
      params: {
        x: 1,
        z: -2,
        dimension: 0,
        sub_chunk_count: 1,
        highest_subchunk_count: undefined,
        cache_enabled: false,
        blobs: [],
        payload: Buffer.from([7]),
        is_client_biome_update: true,
      },
    },
    {
      name: 'dimension_data',
      params: {
        definitions: [
          {
            id: 'minecraft:overworld',
            min_y: -64,
            height_range: 384,
            generator: 'overworld',
            dimension_type: 0,
            pack_id: '00000000-0000-0000-0000-000000000000',
            default_biome: '',
            cloud_height: 192,
            render_clouds: true,
          },
        ],
      },
    },
    {
      name: 'add_entity',
      params: {
        unique_id: 3n,
        runtime_id: 3n,
        entity_type: 'minecraft:pig',
        position: { x: 1, y: 2, z: 3 },
        velocity: { x: 0, y: 0, z: 0 },
        pitch: 0,
        yaw: 0,
        head_yaw: 0,
        body_yaw: 0,
        attributes: [],
        metadata: [],
        properties: { ints: [], floats: [] },
        links: [],
        passenger_block_data: { block_position: { x: 1, y: -2, z: 3 }, offset: { x: 0, y: 0.5, z: 0 }, rotation: 90, rotation_limit: 45, emote_type: 'laying' },
      },
    },
  ]

  for (const packet of packets) {
    const buffer = proto.createPacketBuffer('mcpe_packet', packet)
    const read = proto.parsePacketBuffer('mcpe_packet', buffer)
    assert.equal(read.metadata.size, buffer.length, packet.name)
    assert.deepEqual(proto.createPacketBuffer('mcpe_packet', read.data), buffer, packet.name)
  }
})

void test('a client of a protocol with no changes keeps its own definition', () => {
  const client = { serializer: { proto: 'own' }, deserializer: { proto: 'own' } }
  useProtocol(client as never, '1.26.51', 2193, 2193)
  assert.deepEqual(client, { serializer: { proto: 'own' }, deserializer: { proto: 'own' } })
})
