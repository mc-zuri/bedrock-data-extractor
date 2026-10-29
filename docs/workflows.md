# Workflows

```
track (every 6 hours, or by hand with builds)
  versions/ ─> commit to main
  └─ each build it added (release or preview) ─> support
support (from track, or by hand with a build and its channel)
  linux: export and check; then export and test
    passed ─> pull request "Support BDS <build>": linux "supported"
    failed ─> issue "Support BDS <build>", with the log
extract (push to main: a merged support pull request, or a change to the export)
  each supported build bedrock-data lacks ─> commit to bedrock-data main
```

- [track](../.github/workflows/track.yml) runs `pnpm track` and commits `versions/` to main. It then runs the support
  workflow for each build it added, release or preview. Run by hand with `builds` (and `channel`, `force`) to track
  those.
- [support](../.github/workflows/support.yml) builds the agent, runs `pnpm support --platform=linux` (the Linux zip,
  its server run with the agent), then `pnpm extract --force` and `pnpm test` where it passed. The export goes into a
  copy of bedrock-data that is kept only as the run's artifact, with the agent's log.
  - Where it passes: the pull request `support/<build>` marks linux `"supported"` in `versions/<channel>/<build>.json`
    (or refreshes the open one).
  - Where it fails, or the tests fail: the issue "Support BDS \<build>" with the end of the log (or a comment on the
    open one). A build the agent cannot bind yet is supported by adding its bindings
    ([Development](development.md#a-new-build)) and running support again.
  - CI runs Linux only: a build's Windows zip is supported locally, `pnpm support <build>` on Windows, committed by
    hand.
- [extract](../.github/workflows/extract.yml) builds the agent and runs `pnpm extract` on a push to main that changes
  `versions/release/`, `versions/preview/`, `src/`, `native/` or the dependencies, and commits the result straight to
  bedrock-data's main. A build bedrock-data has the data of is left as it is: to export builds again after a fix, run
  it by hand with `builds` and `force`. A build supported only on Windows (1.21.50.07) is exported locally on Windows,
  its commit naming the extractor commit as extract's do.
- [CI](../.github/workflows/ci.yml) runs `pnpm verify` and `pnpm test`, against bedrock-data's schemas, and builds the
  Linux agent.

The [native](../.github/actions/native/action.yml) action builds the agent (xmake, clang, its packages and build
cached) and puts focal's OpenSSL 1.1 on `LD_LIBRARY_PATH` for the servers before 1.19.40.

## What they need

|                                  |                                                                                            |
| -------------------------------- | ------------------------------------------------------------------------------------------ |
| variable `BEDROCK_DATA_REPO`     | `mc-zuri/bedrock-data`                                                                     |
| secret `BEDROCK_DATA_DEPLOY_KEY` | the private half of a write deploy key of bedrock-data: it can push there and nowhere else |
| Actions setting                  | "Allow GitHub Actions to create and approve pull requests", for support's pull requests    |

track commits and runs support with its own `GITHUB_TOKEN`: a push made with it runs no workflow, a
`workflow_dispatch` does.

## Good to know

- A pull request support opens gets no CI run of its own: GitHub asks for approval to run workflows on pull requests
  the Actions bot opens (the run shows "action required"). The support run that opened it exported and tested it.
- Each support pull request changes only its own build's file, so several can be open and merged in any order.
- track and support check out main as it is when they run, not as it was when they were queued.
