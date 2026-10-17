# bedrock-data-extractor

Tracks every Bedrock Dedicated Server build minecraft.net offers, and exports what each supported release says into
[bedrock-data](https://github.com/mc-zuri/bedrock-data): its language, its block palette and its block shapes.

```
track ─> versions/ ─> support ─> pull request (or issue) ─> merge ─> extract ─> bedrock-data
```

## Use

Node 24, pnpm, [xmake](https://xmake.io) with MSVC (Windows) or clang (Linux), and a bedrock-data checkout next to
this one:

```sh
git clone https://github.com/mc-zuri/bedrock-data ../bedrock-data
pnpm install
pnpm native            # the agent the servers load, for this OS
pnpm track             # the builds minecraft.net offers now, into versions/
pnpm support <build>   # a tracked release tried on this OS's platform, marked supported or not
pnpm extract           # each supported release bedrock-data lacks, into ../bedrock-data
pnpm status            # each build: its platforms, supported or not, and exported
```

## Docs

- [How it works](docs/how-it-works.md): the steps, and the files each writes
- [Workflows](docs/workflows.md): track, support and extract on GitHub, and what they need
- [Development](docs/development.md): the code, the agent, a new build, fixing an export
