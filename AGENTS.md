# AGENTS.md

Read [README.md](README.md) first, and bedrock-data's README: it is the format of everything this writes.

## Invariants

- **bedrock-data is written only by this.** A wrong value is fixed here and the build extracted again
  (`pnpm extract <build> --force`). Never hand-edit bedrock-data, never add an override layer of corrections: where
  extracted data disagrees with another source, find out why and fix the extraction, or document the disagreement.
- **A run changes bedrock-data only where the game says otherwise.** No timestamps, no extractor version, no
  machine-dependent bytes (gzip is rewritten only where its decompressed text changed). `pnpm track` with nothing new,
  or `pnpm extract` of a kept build, must leave `git -C ../bedrock-data status` clean.
- **Exact builds only.** A build is what its zip is named (`1.21.42.01`, never `1.21.42.1`); a zip that is not at its
  URL is a missing platform, never stood in for by a redirect or another build. The channel is the download's, never
  guessed from the number.
- **The schemas are bedrock-data's.** Every write is checked against `BEDROCK_DATA_DIR/schemas/`; the tests copy them
  from there. A new field or file changes bedrock-data's README and schemas together with the code that writes it.
- **Previews are kept by their downloads only.** Data is extracted from releases.

## Conventions

- Node 24 runs `src/*.ts` directly: erasable TypeScript only (no enums, no parameter properties), `.ts` in imports.
- Match the code around you: function declarations, comments that say what a thing is for, no comment noise.
- `pnpm verify && pnpm test` before a commit. A test that needs a zip writes a small one (`test/helpers.ts`
  `writeZip`); none downloads.
