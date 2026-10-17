# bedrock-data-extractor

Tracks every Bedrock Dedicated Server build minecraft.net offers, and exports what each supported release says into
[bedrock-data](https://github.com/mc-zuri/bedrock-data), which
[bedrock-data-generator](https://github.com/mc-zuri/bedrock-data-generator) turns into minecraft-data's bedrock files.

```
track ─> versions/ ─> support ─> pull request (or issue) ─> merge ─> extract ─> bedrock-data
```

## Use

Node 24, pnpm, and a bedrock-data checkout next to this one:

```sh
git clone https://github.com/mc-zuri/bedrock-data ../bedrock-data
pnpm install
pnpm track             # the builds minecraft.net offers now, into versions/
pnpm support <build>   # each platform of a tracked release tried, those that pass marked supported
pnpm extract           # each supported release bedrock-data lacks, into ../bedrock-data
pnpm status            # each build: its platforms, supported or not, and exported
```

## Docs

- [How it works](docs/how-it-works.md): the steps, and the files each writes
- [Workflows](docs/workflows.md): track, support and extract on GitHub, and what they need
- [Development](docs/development.md): the code, the tests, fixing an export, what comes next
