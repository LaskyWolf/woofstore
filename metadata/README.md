# WoofStore metadata

This folder adds names, descriptions, icons and screenshots for packages that Flathub and Arch do not describe: CachyOS-only packages and AUR packages. WoofStore also fixes app merging with `merge-overrides.json`.

WoofStore reads this folder from `share/woofstore/metadata` after `cmake --install`, and from the source tree in development builds.

## Add an app

1. Draft an entry. The draft tool reads the AUR entry and the project's README:

   ```bash
   python3 tools/draft-metadata.py <aur-package-name> --no-ai
   ```

   Without `--no-ai`, the tool asks Claude to draft the summary and description (needs `ANTHROPIC_API_KEY` and `python -m pip install --user anthropic`). It never creates images; it lists real image URLs from the README as comments.

2. Check every line against the project's own website or README. Remove anything you cannot confirm.
3. Paste the `<component>` into `xml/woofstore.xml`. The `<pkgname>` must be the exact repo or AUR package name: that is how WoofStore attaches the entry to the package.
4. Optional icon: a 64 × 64 PNG published by the project, saved as `icons/woofstore/64x64/<id>.png` and referenced by `<icon type="cached" width="64" height="64"><id>.png</icon>`.
5. Run `./build/woof-store --catalog-stats` and check that the `WoofStore metadata` line counts the new entry and its icon.

Rules: use only the publisher's own text, icons and screenshots. AI-generated images are never allowed.

## Fix a merge

`merge-overrides.json` corrects automatic merging:

- `"merge": {"<package name>": "<AppStream ID>"}` joins a package to an app that has a different name or website.
- `"separate": ["<package name>"]` keeps a package as its own entry.

To find candidates, run `WOOFSTORE_SHOW_MERGES=1 ./build/woof-store --catalog-stats`. It prints every merge and every near miss (same name, different project).

## Sources of the bundled files

| File | Source |
| --- | --- |
| `icons/woofstore/64x64/org.cachyos.hello.png` | `cachyos-hello` package, `org.cachyos.hello.svg`, rendered at 64 px |
| `icons/woofstore/64x64/org.cachyos.KernelManager.png` | `cachyos-kernel-manager` package, 64 × 64 icon |
| `icons/woofstore/64x64/org.cachyos.PackageInstaller.png` | `cachyos-packageinstaller` package, `cachyos-pi.png`, resized to 64 px |
| CachyOS Welcome screenshot | [cachyos-welcome README](https://github.com/cachyos/cachyos-welcome) |
