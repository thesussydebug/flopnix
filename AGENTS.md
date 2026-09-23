# Project instructions

Read the relevant documentation in docs/ before changing the project.

## Required build delivery

The user requires every completed OS build to refresh the full copy-ready
artifact set in built/:

- built/flopnix.img: the newly built floppy image.
- built/flopnix.ku: the matching kernel update.
- built/kexts/*.kx: the complete matching set of production extensions.

Use build.sh, which already stages this set automatically. Do not leave a
completed build only in out/ or the repository root. Do not ship test-only
extensions. Verify that the staged artifacts match the build outputs before
reporting completion. Keep this behavior when changing the build workflow.

## Repository scope

Keep this repository limited to OS source, build documentation, essential
build helpers, and the existing out/ and built/ build structure.
FLOPNIX Manager belongs in the sibling flopnix-manager folder. Never bundle,
copy, or build the Manager as part of the OS build. Its symbols, captures,
and tests also stay outside this repository.
Keep development probes, optional utilities, test runners, historical outputs,
and scratch media in the sibling flopnix-devtools folder. Preserve material
by moving it outside the repo when its value is uncertain. Ignore generated
artifacts and do not introduce extra tools or outputs into the repo.
