# AGENTS.md

Read [README.md](README.md) and [docs/](docs) first, and bedrock-data's README: it is the format of everything extract
writes.

## Invariants

- **Each file has one writer.** `versions/`: `pnpm track`. `supported.json`: `pnpm support`, through a pull request.
  bedrock-data: `pnpm extract`. Never hand-edit any of them, never add an override layer of corrections: where
  extracted data disagrees with another source, find out why and fix the extraction, or document the disagreement.
- **A run changes files only where the game says otherwise.** No timestamps, no extractor version, no
  machine-dependent bytes (gzip is rewritten only where its decompressed text changed). `pnpm track` with nothing new,
  or `pnpm extract` with nothing unexported, must leave both repos' `git status` clean.
- **Only supported releases are exported, and an export is never repeated unasked.** `pnpm extract` skips a release
  bedrock-data has the data of; `--force` exports it again.
- **Exact builds only.** A build is what its zip is named (`1.21.42.01`, never `1.21.42.1`); a zip that is not at its
  URL is a missing platform, never stood in for by a redirect or another build. The channel is the download's, never
  guessed from the number. `extract` reads only the zip `versions/` hashed.
- **Schemas and checks first.** `versions/` and `supported.json` are checked against `schemas/` here; bedrock-data's
  files against `BEDROCK_DATA_DIR/schemas/` (the tests copy them from there), and extracted data against
  `src/checks.ts`. A new field or file changes its schema, and for bedrock-data its README, together with the code that
  writes it.
- **Previews are tracked, never supported.**

## Conventions

- Node 24 runs `src/*.ts` directly: erasable TypeScript only (no enums, no parameter properties), `.ts` in imports.
- Match the code around you: function declarations, comments that say what a thing is for, no comment noise.
- `pnpm verify && pnpm test` before a commit. A test that needs a zip writes a small one (`test/helpers.ts`
  `writeZip`); none downloads.
