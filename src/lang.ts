// A .lang file's keys and strings, read as bedrock-data's README (language.json.gz) says. The game's files have comments
// a line of their own (# ...), after a string (<tab>## ..., <spaces>## ..., ##...), and strings that hold a # of their
// own (Tap # in hotbar) or an = (@e = all entities).

/** Where the string's inline comment starts: the first ##, or a tab followed (after any more blanks) by #. */
function commentAt(rest: string): number {
  const hashes = rest.indexOf('##')
  const tab = /\t[ \t]*#/.exec(rest)?.index ?? -1
  if (hashes < 0) return tab
  return tab < 0 ? hashes : Math.min(hashes, tab)
}

/** Each key and its string in the file's order; a key said twice keeps its first place and its last string. */
export function parseLang(text: string): Map<string, string> {
  const strings = new Map<string, string>()
  for (const raw of text.replace(/^﻿/, '').split('\n')) {
    const line = raw.replace(/\r$/, '')
    const start = line.trimStart()
    if (!start || start.startsWith('#')) continue
    const eq = line.indexOf('=')
    if (eq < 0) continue
    const key = line.slice(0, eq).trim()
    if (!key) continue
    const rest = line.slice(eq + 1)
    const comment = commentAt(rest)
    strings.set(key, (comment < 0 ? rest : rest.slice(0, comment)).trimEnd())
  }
  return strings
}
