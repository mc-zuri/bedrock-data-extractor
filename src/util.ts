import { existsSync, readFileSync, renameSync, writeFileSync } from 'node:fs'

export function errorMessage(e: unknown): string {
  return e instanceof Error ? e.message : String(e)
}

export function readIfExists(file: string): Buffer | undefined {
  return existsSync(file) ? readFileSync(file) : undefined
}

/** Through a temp file: an interrupted run never leaves half a file that a later run would take as done. */
export function writeAtomic(file: string, data: string | Buffer): void {
  writeFileSync(`${file}.tmp`, data)
  renameSync(`${file}.tmp`, file)
}

function secondsSince(start: number): string {
  return `${((performance.now() - start) / 1000).toFixed(1)} s`
}

/**
 * Runs `work` on each item, `jobs` at a time, and prints what it returns under the item's name. An item that throws is
 * reported, and returned among the failures; the others go on.
 */
export async function forEach<T>(todo: T[], jobs: number, name: (item: T) => string, work: (item: T) => Promise<string | undefined>): Promise<string[]> {
  const failed: string[] = []
  let next = 0
  async function worker(): Promise<void> {
    while (next < todo.length) {
      const item = todo[next++]
      const start = performance.now()
      try {
        const done = await work(item)
        if (done) console.log(`  ${name(item)}: ${done} (${secondsSince(start)})`)
      } catch (e) {
        failed.push(name(item))
        console.error(`  ${name(item)}: FAILED: ${errorMessage(e)} (${secondsSince(start)})`)
      }
    }
  }
  await Promise.all(Array.from({ length: Math.min(jobs, todo.length) }, worker))
  return failed
}
