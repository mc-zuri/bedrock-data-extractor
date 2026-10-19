// What a server's console answers `help <command>` for each command: the command's name and aliases, its description,
// and a usage line per overload, naming each parameter's type as the game does ("x y z", "wildcard int"). Cheats are
// allowed first: before 1.21.0 the console describes only the commands that need none otherwise.
import type { RunningServer } from './server.ts'

// how long the console stays silent after the last line of an answer
const QUIET_MS = 400

/** what the console printed, a line each, without the log's prefix and the colours */
function lines(said: string): string[] {
  return said
    .split(/\r?\n/)
    .map((line) => line.replace(/^(NO LOG FILE! - )?\[[^\]]*\] /, '').replace(/§./g, ''))
    .filter((line) => line.trim() !== '')
}

// what the server logs of a player while it answers (the client has just left)
const PLAYER_LOG = /^Player (connected|disconnected|Spawned)[:,]/

/**
 * The answer from the command's header on ("teleport (also tp):") to its last usage line, without what the server logs
 * of a player meanwhile: the server may still be printing something else when it is asked.
 */
export function helpOf(command: string, said: string): string[] {
  const all = lines(said).filter((line) => !PLAYER_LOG.test(line))
  const header = all.findIndex((line) => line === `${command}:` || line.startsWith(`${command} (also `))
  const answer = header < 0 ? all : all.slice(header)
  const last = answer.findLastIndex((line) => line.startsWith('- /'))
  return last < 0 ? answer : answer.slice(0, last + 1)
}

function answered(said: string): boolean {
  return lines(said).some((line) => line.startsWith('- /') || line.includes('Unknown command') || line.includes('Syntax error'))
}

/** Each of `commands` with the console's help for it. */
export async function commandHelp(server: RunningServer, commands: string[]): Promise<Record<string, string[]>> {
  await server.command('changesetting allow-cheats true', ['allow-cheats', 'Syntax error', 'Unknown command'])
  const help: Record<string, string[]> = {}

  for (const command of commands) {
    help[command] = helpOf(command, await server.answer(`help ${command}`, answered, QUIET_MS))
  }

  return help
}
