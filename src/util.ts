import { existsSync, readFileSync, renameSync, writeFileSync } from 'node:fs'

/** The shortest decimal that reads back as the same float32 (what the game's floats are). */
export function f32(value: number): number {
  if (!Number.isFinite(value) || Math.fround(value) !== value) {
    return value
  }

  for (let precision = 1; precision < 17; precision++) {
    const candidate = Number(value.toPrecision(precision))

    if (Math.fround(candidate) === value) {
      return candidate
    }
  }

  return value
}

export function errorMessage(e: unknown): string {
  return e instanceof Error ? e.message : String(e)
}

export function readIfExists(file: string): Buffer | undefined {
  return existsSync(file) ? readFileSync(file) : undefined
}

export function writeAtomic(file: string, data: string | Buffer): void {
  writeFileSync(`${file}.tmp`, data)
  renameSync(`${file}.tmp`, file)
}

function secondsSince(start: number): string {
  return `${((performance.now() - start) / 1000).toFixed(1)} s`
}

/**
 * `work` on each of `todo`, `jobs` at a time; the name of the one that failed. The first failure stops the rest: none is
 * started after it, and the ones running are told to stop through the signal.
 */
export async function forEach<T>(todo: T[], jobs: number, name: (item: T) => string, work: (item: T, signal: AbortSignal) => Promise<string | undefined>): Promise<string[]> {
  const failed: string[] = []
  const stop = new AbortController()
  let next = 0
  async function worker(): Promise<void> {
    while (next < todo.length && !stop.signal.aborted) {
      const item = todo[next++]
      const start = performance.now()

      try {
        const done = await work(item, stop.signal)

        if (done) {
          console.log(`  ${name(item)}: ${done} (${secondsSince(start)})`)
        }
      } catch (e) {
        if (stop.signal.aborted) {
          console.error(`  ${name(item)}: stopped (${secondsSince(start)})`)
          continue
        }

        failed.push(name(item))
        console.error(`  ${name(item)}: FAILED: ${errorMessage(e)} (${secondsSince(start)})`)
        stop.abort()
      }
    }
  }
  await Promise.all(Array.from({ length: Math.min(jobs, todo.length) }, worker))

  if (next < todo.length) {
    console.error(`  ${todo.length - next} not started: ${todo.slice(next).map(name).join(', ')}`)
  }

  return failed
}
