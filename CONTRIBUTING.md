# Contributing

Thank you for improving this repository.

## Workflow

1. Open an issue for behavior changes or larger maintenance work.
2. Create a topic branch with a concise repository-style name.
3. Keep changes focused and avoid committing generated build outputs.
4. Update documentation when paths, build behavior, or firmware artifacts
   change.
5. Open a pull request and complete the pull request template.

## Validation

Use GitHub Actions as the source of build validation. The CI workflow builds
first-party ESP-IDF and Arduino examples and uploads ESP-IDF firmware artifacts
after successful builds.

Do not include local machine paths, usernames, tool installation paths, or other
host-specific details in public issue, pull request, or release text.
