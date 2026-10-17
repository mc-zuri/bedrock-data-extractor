function commentAt(rest: string): number {
  const hashes = rest.indexOf('##')
  const tab = /\t[ \t]*#/.exec(rest)?.index ?? -1

  if (hashes < 0) {
    return tab
  }

  return tab < 0 ? hashes : Math.min(hashes, tab)
}

export function parseLang(text: string): Map<string, string> {
  const strings = new Map<string, string>()

  for (const raw of text.replace(/^﻿/, '').split('\n')) {
    const line = raw.replace(/\r$/, '')
    const start = line.trimStart()

    if (!start || start.startsWith('#')) {
      continue
    }

    const eq = line.indexOf('=')

    if (eq < 0) {
      continue
    }

    const key = line.slice(0, eq).trim()

    if (!key) {
      continue
    }

    const rest = line.slice(eq + 1)
    const comment = commentAt(rest)
    strings.set(key, (comment < 0 ? rest : rest.slice(0, comment)).trimEnd())
  }

  return strings
}
