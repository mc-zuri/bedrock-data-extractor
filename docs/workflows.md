# Workflows

```
track (every 6 hours, or by hand with builds)
  versions/ ─> commit to main
  └─ each release it added ─> support
support (from track, or by hand with a build)
  add to supported.json, export, test
    ok   ─> pull request "Support BDS <build>"
    fail ─> issue "Support BDS <build>", with the log
extract (push to main: a merged support pull request, or a change to the export)
  each supported release bedrock-data lacks ─> commit to bedrock-data main
```

- [track](../.github/workflows/track.yml) runs `pnpm track` and commits `versions/` to main. It then runs the support
  workflow for each release it added. Run by hand with `builds` (and `channel`, `force`) to track those.
- [support](../.github/workflows/support.yml) runs `pnpm support`, `pnpm extract --force` and `pnpm test`. The export
  goes into a copy of bedrock-data that is kept only as the run's artifact.
  - Where all pass, it opens (or refreshes) the pull request `support/<build>` that adds the build to `supported.json`.
  - Where one fails, it opens the issue "Support BDS \<build>" with the end of the log, or comments on the open one. A
    build the export cannot handle yet is supported by fixing the export and adding the build to `supported.json` in
    the same pull request.
- [extract](../.github/workflows/extract.yml) runs `pnpm extract` on a push to main that changes `supported.json`,
  `src/` or the dependencies, and commits the result straight to bedrock-data's main. A release bedrock-data has the
  data of is left as it is: to export releases again after a fix, run it by hand with `builds` and `force`.
- [CI](../.github/workflows/ci.yml) runs `pnpm verify` and `pnpm test`, against bedrock-data's schemas.

bedrock-data-generator picks bedrock-data's new commits up on its own schedule, half an hour after track's (or at
once, through bedrock-data's notify-generator workflow, where that has a token).

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
- Two support pull requests open at once both change `supported.json`: once one is merged, the other conflicts. Run
  support again for that build; it rebuilds the branch from main.
- track and support check out main as it is when they run, not as it was when they were queued.
