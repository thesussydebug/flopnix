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
