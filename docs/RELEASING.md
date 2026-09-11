# Preparing a release

1. Check the version in `src/os.h` and `README.md`.
2. Run `bash build.sh` from the project folder.
3. Run `python tools/checkbuild.py` and `python tools/test_kapidoc.py`.
4. Run the guest tests with `-Cpu pentium2` and `-Cpu qemu32`. Run targeted
   tests for the systems changed in this release.
5. Publish `built/flopnix.img`, `built/flopnix.ku`, and the complete
   `built/kexts/` set together. Include `built/README.txt` if packaging a ZIP.
6. Describe the changes, tested configurations, and known limits in the
   release notes. Choose the release tag when publishing.

Do not include `out/`, local `usb.img`, test `.kx` files, Python caches, or
historical backups in source commits or release downloads. `.gitignore`
excludes these files. Generated API and app tables are rebuilt from source.

The source folder is ready to import into a Git repository. Repository
creation, remote selection, commits, tags, and publication are separate
steps. No license has been selected in this checkout; add the intended
license before presenting the project as open source.

The September 2026 cleanup moved historical images, screenshots, debugging
runs, sample assets, and the existing USB image into a sibling
`flopnix-extras` folder. It also saved the previous source tree there.
That archive is local history and is not a build dependency.
