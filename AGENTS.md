# AGENTS.md

Read [README.md](README.md) first, and bedrock-data's README: it is the format of everything extract writes.

## Invariants

- **`versions/` is written only by `pnpm track`, bedrock-data only by `pnpm extract`.** A wrong value is fixed here and
  extracted again (a push of the fix to main does so). Never hand-edit either, never add an override layer of
  corrections: where extracted data disagrees with another source, find out why and fix the extraction, or document
  the disagreement.
- **A run changes files only where the game says otherwise.** No timestamps, no extractor version, no
  machine-dependent bytes (gzip is rewritten only where its decompressed text changed). `pnpm track` with nothing new,
  or `pnpm extract` of a release bedrock-data has, must leave both repos' `git status` clean.
- **Exact builds only.** A build is what its zip is named (`1.21.42.01`, never `1.21.42.1`); a zip that is not at its
  URL is a missing platform, never stood in for by a redirect or another build. The channel is the download's, never
  guessed from the number. `extract` reads only the zip `versions/` hashed.
- **Schemas first.** `versions/` is checked against `schemas/` here; bedrock-data's files against
  `BEDROCK_DATA_DIR/schemas/` (the tests copy them from there). A new field or file changes its schema, and for
  bedrock-data its README, together with the code that writes it.
- **Previews are tracked, never extracted.**

## Conventions

- Node 24 runs `src/*.ts` directly: erasable TypeScript only (no enums, no parameter properties), `.ts` in imports.
- Match the code around you: function declarations, comments that say what a thing is for, no comment noise.
- `pnpm verify && pnpm test` before a commit. A test that needs a zip writes a small one (`test/helpers.ts`
  `writeZip`); none downloads.
