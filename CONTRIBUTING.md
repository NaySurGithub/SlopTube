# Contributing to SlopTube

Thanks for helping improve SlopTube. You can contribute code, review a
translation, improve the documentation, or report a reproducible bug.

## Choose a target branch

- **`dev`** is the base branch for features, translations, documentation,
  refactors, and routine bug fixes. GitHub may preselect `main`, so check the
  base branch before opening your pull request.
- **`main`** is reserved for urgent fixes that need to reach users before the
  next release from `dev`, such as security fixes or crashes in the current
  release. Explain in the pull request why it needs to go directly to `main`.

Create a branch from the appropriate base in your fork. For a normal change,
sync your fork's `dev` with upstream first, then branch from it.

## Report an issue

Check the existing issues for the same problem. For an Android bug, include
the SlopTube version, device model, Android version, steps to reproduce, and
what you expected to happen. Mention whether the issue happens when signed in
or signed out if it affects playback or account features. If a public video
reproduces the problem, include its URL.

A short screenshot, recording, or relevant excerpt from `adb logcat` can help.
Remove account details, cookies, and tokens before posting logs.

## Make and verify a change

Read the [build guide](docs/building.md) for prerequisites and installation.
Build the APK from the command line with:

```sh
cmake --preset android-release
cmake --build --preset android-release
```

For a change to the shared core or desktop CLI, also build the `host-debug`
preset. For Android UI or playback changes, try the affected flow on a device
and note the device and Android version in the PR.

Format changed C++ files with the repository's `.clang-format`. CI checks
formatting and builds both the CLI and Android APK. See the
[code conventions](docs/contributing.md) and the
[translation guide](docs/i18n.md) for details specific to those changes.

## Write commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/) for every
commit. Write the subject as `type(scope): description`. The scope is optional.
Use `feat` for a feature, `fix` for a bug fix, and `docs` for documentation.

- `feat(settings): add a language picker`
- `fix(player): resume playback after seeking`
- `docs: explain how to install the APK`

## Open a pull request

Keep each PR focused on one change so it can be reviewed on its own. Describe
what changed, why it matters, and how you verified it. Link the relevant
issue if there is one. For visible changes, attach screenshots or a short
recording. For translations, mention which screens you checked in the app.
