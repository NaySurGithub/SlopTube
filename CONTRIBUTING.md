# Contributing to SlopTube

Open pull requests against `dev` for features, translations, refactors, and routine
bug fixes. When opening a pull request on GitHub, select `dev` as the base branch.

Target `main` only for urgent fixes that need to reach users before the next
release from `dev`, such as security fixes or fixes for crashes in the current
release. Explain in the pull request why the fix needs to go directly to `main`.

## Commit messages

Use [Conventional Commits](https://www.conventionalcommits.org/) for every
commit. Write the subject as `type(scope): description`. The scope is optional.
For example, use `feat(settings): add Spanish translations` or
`fix(player): resume playback after seeking`.
