import { dirname, isAbsolute, join, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..')

function pathSetting(name: string, fallback: string): string {
  const value = process.env[name] || fallback
  return isAbsolute(value) ? resolve(value) : resolve(ROOT, value)
}

function positiveIntSetting(name: string, fallback: number): number {
  const value = Number(process.env[name] || fallback)
  if (!Number.isInteger(value) || value < 1) throw new Error(`${name} must be a positive integer, not ${process.env[name]}`)
  return value
}

// the builds minecraft.net has offered, kept in this repo, and the schemas of its files
export const VERSIONS_DIR = join(ROOT, 'versions')
export const SCHEMAS_DIR = join(ROOT, 'schemas')
// the releases exported to bedrock-data
export const SUPPORTED_FILE = join(ROOT, 'supported.json')
// the bedrock-data checkout the extract step writes into
export const BEDROCK_DATA_DIR = pathSetting('BEDROCK_DATA_DIR', '../bedrock-data')
// a build's zips while it is extracted
export const CACHE_DIR = pathSetting('CACHE_DIR', 'cache')
export const JOBS = positiveIntSetting('JOBS', 3)
