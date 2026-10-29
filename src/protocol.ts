// The packets of protocols bedrock-protocol knows no version of (every preview's): what each protocol changed in what a
// server sends, as Mojang's protocol docs give it (github.com/Mojang/bedrock-protocol-docs, a tag per preview). A
// client decoding as the newest version it knows before a server's protocol (src/client.ts) decodes with that version's
// definition (minecraft-data's) changed by each protocol after it up to the server's.
import type { Client } from 'bedrock-protocol'
import { createRequire } from 'node:module'

/** A type of the definition (ProtoDef): a name, or a constructor and its options */
type Type = string | [string, unknown]

interface Field {
  name?: string
  anon?: boolean
  type: Type
}

/**
 * One change to the definition: a type defined anew, or a field added to (after `after`, else last) or dropped from a
 * container: the type's own, or the one `path` leads to in it (a field's name, or a case of a switch).
 */
export interface Change {
  /** the first protocol with it */
  protocol: number
  type: string
  path?: string[]
  define?: Type
  add?: Field
  after?: string
  drop?: string
}

// a TransitionSettingsData's fields; a NoiseTransitionSettingsData's begin with them
const TRANSITION_SETTINGS: Field[] = [
  { name: 'total_transition_ticks', type: 'varint' },
  { name: 'current_transition_ticks', type: 'varint' },
  { name: 'easing', type: 'zigzag32' },
  { name: 'clock_name', type: 'string' },
]

// the attribute an EnvironmentAttributeData sets (BoolAttributeData, FloatAttributeData, ColorAttributeData,
// StringAttributeData from 2235), as minecraft-data has the first three
function attributeData(withString: boolean): Type {
  const fields: Record<string, Type> = {
    bool: [
      'container',
      [
        { name: 'bool_value', type: 'bool' },
        { name: 'bool_operation', type: ['option', 'li32'] },
      ],
    ],
    float: [
      'container',
      [
        { name: 'float_value', type: 'lf32' },
        { name: 'float_operation', type: ['option', 'li32'] },
        { name: 'float_constraint_min', type: ['option', 'lf32'] },
        { name: 'float_constraint_max', type: ['option', 'lf32'] },
      ],
    ],
    colour: [
      'container',
      [
        { name: 'colour_value', type: 'li32' },
        { name: 'colour_operation', type: ['option', 'li32'] },
      ],
    ],
  }
  const mappings: Record<string, string> = { 0: 'bool', 1: 'float', 2: 'colour' }

  if (withString) {
    fields.string = [
      'container',
      [
        { name: 'string_value', type: 'string' },
        { name: 'string_operation', type: ['option', 'li32'] },
      ],
    ]
    mappings[3] = 'string'
  }

  return [
    'container',
    [
      { name: 'type', type: ['mapper', { type: 'varint', mappings }] },
      { anon: true, type: ['switch', { compareTo: 'type', fields }] },
    ],
  ]
}

// each change, in protocol order
export const CHANGES: Change[] = [
  // 1.26.60.21: AnimatePacket's optional Hand (HandSlot)
  { protocol: 2207, type: 'packet_animate', add: { name: 'hand', type: ['option', ['mapper', { type: 'u8', mappings: { 0: 'mainhand', 1: 'offhand' } }]] } },
  // AttributeLayerData without NoiseName; EnvironmentAttributeData: a name and one of a constant, a transition and a
  // noise transition (ConstantAttributeData, TransitionAttributeData, NoiseTransitionAttributeData)
  { protocol: 2207, type: 'AttributeLayerData', drop: 'noise_name' },
  {
    protocol: 2207,
    type: 'EnvironmentAttributeData',
    define: [
      'container',
      [
        { name: 'attribute_name', type: 'string' },
        { name: 'payload_type', type: ['mapper', { type: 'varint', mappings: { 0: 'constant', 1: 'transition', 2: 'noise_transition' } }] },
        {
          anon: true,
          type: [
            'switch',
            {
              compareTo: 'payload_type',
              fields: {
                constant: ['container', [{ name: 'attribute', type: 'AttributeData' }]],
                transition: [
                  'container',
                  [
                    { name: 'from_attribute', type: 'AttributeData' },
                    { name: 'to_attribute', type: 'AttributeData' },
                    { name: 'settings', type: ['container', TRANSITION_SETTINGS] },
                  ],
                ],
                noise_transition: [
                  'container',
                  [
                    { name: 'from_attribute', type: 'AttributeData' },
                    { name: 'to_attribute', type: 'AttributeData' },
                    {
                      name: 'settings',
                      type: [
                        'container',
                        [
                          ...TRANSITION_SETTINGS,
                          { name: 'local_transition_ticks', type: 'varint' },
                          { name: 'noise_name', type: 'string' },
                          {
                            name: 'noise_alignment',
                            type: [
                              'container',
                              [
                                { name: 'type', type: 'u8' },
                                { name: 'value', type: 'varint' },
                              ],
                            ],
                          },
                        ],
                      ],
                    },
                  ],
                ],
              },
            },
          ],
        },
      ],
    ],
  },
  // DimensionDefinition's Cloud Height and Render Clouds
  { protocol: 2207, type: 'packet_dimension_data', path: ['definitions'], after: 'default_biome', add: { name: 'cloud_height', type: 'zigzag32' } },
  { protocol: 2207, type: 'packet_dimension_data', path: ['definitions'], after: 'cloud_height', add: { name: 'render_clouds', type: 'bool' } },
  // the PlayFab id moved from SerializedSkin to PlayerListAddEntry, after the XUID
  { protocol: 2207, type: 'PlayerRecord', path: ['add'], after: 'xbox_user_id', add: { name: 'play_fab_id', type: 'string' } },
  { protocol: 2207, type: 'Skin', drop: 'play_fab_id' },
  // 1.26.60.23: LevelChunkPacket's Is Client Biome Update, after the chunk's data
  { protocol: 2208, type: 'packet_level_chunk', add: { name: 'is_client_biome_update', type: 'bool' } },
  // 1.26.60.28: LevelSettings' Editor Level Migration Version (an int8 enum), after Is Exported From Editor
  { protocol: 2216, type: 'packet_start_game', after: 'exported_from_editor', add: { name: 'editor_level_migration_version', type: 'i8' } },
  // 1.26.60.29: AddActorPacket's and AddPlayerPacket's optional Passenger Block Data (PassengerOfBlockArguments)
  {
    protocol: 2223,
    type: 'PassengerOfBlockArguments',
    define: [
      'container',
      [
        { name: 'block_position', type: 'BlockCoordinates' },
        { name: 'offset', type: 'vec3f' },
        { name: 'rotation', type: 'lf32' },
        { name: 'rotation_limit', type: 'lf32' },
        { name: 'emote_type', type: ['mapper', { type: 'u8', mappings: { 0: 'standing', 1: 'riding', 2: 'laying' } }] },
      ],
    ],
  },
  { protocol: 2223, type: 'packet_add_entity', add: { name: 'passenger_block_data', type: ['option', 'PassengerOfBlockArguments'] } },
  { protocol: 2223, type: 'packet_add_player', after: 'links', add: { name: 'passenger_block_data', type: ['option', 'PassengerOfBlockArguments'] } },
  // 1.26.70.20: an attribute may be a string (StringAttributeData); CommandOutputMessage's Raw Message; server_config's
  // optional managedChat (managedChatJoinInfo)
  { protocol: 2235, type: 'AttributeData', define: attributeData(true) },
  { protocol: 2235, type: 'packet_command_output', path: ['output'], after: 'parameters', add: { name: 'raw_message', type: 'string' } },
  {
    protocol: 2235,
    type: 'ServerJoinInformation',
    after: 'presence_info',
    add: { name: 'managed_chat_info', type: ['option', ['container', [{ name: 'enabled', type: 'bool' }]]] },
  },
]

// an array's or an option's type, through each
function unwrapped(type: Type | undefined): Type | undefined {
  let at = type

  while (Array.isArray(at) && (at[0] === 'array' || at[0] === 'option')) {
    at = at[0] === 'array' ? (at[1] as { type: Type }).type : (at[1] as Type)
  }

  return at
}

// a switch's cases
function casesOf(type: Type | undefined): Record<string, Type> | undefined {
  return Array.isArray(type) && type[0] === 'switch' ? (type[1] as { fields: Record<string, Type> }).fields : undefined
}

// the container a path leads to in a type: each step a field's name or a case of a switch (an anonymous one's too)
function containerAt(type: Type, path: string[], where: string): Field[] {
  let at: Type | undefined = type

  for (const step of path) {
    at = unwrapped(at)
    const cases = casesOf(at)

    if (cases) {
      at = cases[step]
      continue
    }

    const fields = Array.isArray(at) && at[0] === 'container' ? (at[1] as Field[]) : []
    at = fields.find((f) => f.name === step)?.type ?? fields.map((f) => (f.anon ? casesOf(f.type)?.[step] : undefined)).find(Boolean)
  }

  at = unwrapped(at)

  if (!Array.isArray(at) || at[0] !== 'container') {
    throw new Error(`the protocol: ${where} is no container`)
  }

  return at[1] as Field[]
}

/** `types` (minecraft-data's definition of a version of protocol `from`) with each change after `from` up to `to` */
export function changedTypes(types: Record<string, Type>, from: number, to: number, changes: Change[] = CHANGES): Record<string, Type> {
  const out = structuredClone(types)

  for (const c of changes.filter((c) => c.protocol > from && c.protocol <= to)) {
    const where = [c.type, ...(c.path ?? [])].join('.')

    if (c.define) {
      out[c.type] = c.define
      continue
    }

    if (!(c.type in out)) {
      throw new Error(`the protocol: no ${c.type} to change for ${c.protocol}`)
    }

    const fields = containerAt(out[c.type], c.path ?? [], where)

    if (c.drop) {
      const i = fields.findIndex((f) => f.name === c.drop)

      if (i < 0) {
        throw new Error(`the protocol: ${where} has no ${c.drop} to drop for ${c.protocol}`)
      }

      fields.splice(i, 1)
    }

    if (c.add) {
      const i = c.after === undefined ? fields.length : fields.findIndex((f) => f.name === c.after) + 1

      if (i <= 0) {
        throw new Error(`the protocol: ${where} has no ${c.after} to add ${c.add.name} after for ${c.protocol}`)
      }

      fields.splice(i, 0, c.add)
    }
  }

  return out
}

interface Compiled {
  createPacketBuffer(type: string, packet: unknown): Buffer
}

const compiled = new Map<string, Compiled>()

/** Has `client`, decoding as `version` (of protocol `from`), read and write protocol `to`'s packets */
export function useProtocol(client: Client, version: string, from: number, to: number): void {
  if (!CHANGES.some((c) => c.protocol > from && c.protocol <= to)) {
    return
  }

  const key = `${version}/${to}`
  let proto = compiled.get(key)

  if (!proto) {
    const require = createRequire(createRequire(import.meta.url).resolve('bedrock-protocol'))
    const { ProtoDefCompiler } = (require('protodef') as { Compiler: { ProtoDefCompiler: new () => ProtoDefCompiler } }).Compiler
    const types = (require('minecraft-data') as (v: string) => { protocol: { types: Record<string, Type> } })(`bedrock_${version}`).protocol.types
    const compiler = new ProtoDefCompiler()
    compiler.addTypesToCompile(changedTypes(types, from, to))
    compiler.addTypes(require('./src/datatypes/compiler-minecraft'))
    proto = compiler.compileProtoDefSync()
    compiled.set(key, proto)
  }

  const codecs = client as unknown as { serializer: { proto: Compiled }; deserializer: { proto: Compiled } }
  codecs.serializer.proto = proto
  codecs.deserializer.proto = proto
}

interface ProtoDefCompiler {
  addTypesToCompile(types: Record<string, Type>): void
  addTypes(types: unknown): void
  compileProtoDefSync(): Compiled
}
