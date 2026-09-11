# Contributing to FLOPNIX

Read [the architecture](docs/ARCHITECTURE.md) and
[maintenance guide](docs/MAINTAINING.md) before changing kernel code.
Use [the app tutorial](docs/WRITING-APPS.md) for extension work.

- Keep changes focused and preserve existing files and settings.
- Use short comments for behavior, units, ownership, or limits that are not
  clear from the code. Describe the current behavior, not the history of a fix.
- Put longer explanations in `docs/`. Keep the README version in step with
  `OS_VER` in `src/os.h`.
- Append API fields instead of moving existing ones. Bump the appropriate
  version when the interface grows.
- Keep test extensions out of the production list in `build.sh`.
- Run the relevant regression tests and both guest CPU profiles for shared
  kernel changes. Include the actual results with the change.
- Build with `build.sh`, then run `python tools/checkbuild.py`. Every delivery
  must include matching image, kernel update, and production extensions.

Build output, local USB images, logs, and caches do not belong in source
commits. Publish the contents of `built/` as release assets. See
[the release guide](docs/RELEASING.md) for the final checks.
