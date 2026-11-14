# projectmem - BrcmPatchRAM

_Last updated: 2026-11-14_

## Project purpose
BrcmPatchRAM ============

## Recent issues
- [DONE] #legacy_bdb3 Legacy issue: Update BlueToolFixup for macOS 15 (#33) -> Update BlueToolFixup for macOS 15 (#33) (fixed)
- [DONE] #legacy_9cb4 Legacy issue: Try to fix CI -> Try to fix CI (fixed)
- [DONE] #legacy_5b45 Legacy issue: Fix bluetoothInternalControllerInfo value in README -> Fix bluetoothInternalControllerInfo value in README (fixed)
- [DONE] #legacy_4f07 Legacy issue: Remove NVRAM check patch by default in BluetoolFixup -> Remove NVRAM check patch by default in BluetoolFixup (fixed)
- [DONE] #legacy_4a1a Legacy issue: BlueToolFixup: Add another Vendor callback patch to enable Bluetooth turn on/off after power cycle which was changed from Ventura 13.3. (#30) -> BlueToolFixup: Add another Vendor callback patch to enable Bluetooth turn on/off after power cycle which was changed from Ventura 13.3. (#30) (fixed)
- [DONE] #legacy_2305 Legacy issue: Bump version and fixed Changelog -> Bump version and fixed Changelog (fixed)

## Decisions
- No decisions logged yet.

## Notes
- Update CI badge link
- Disabled automatic `board-id` patch in bluetoothd on macOS 14+
- Disabled chipset patch on macOS 15 as it is 13.x specific
- Bump version
- Added constants for macOS 26 support
- Bump version
- Add chipset patch for macOS 15.4 and above
- Docs: Sync changelog
- Bump version
- New feature: add codegraph and projectmem [.codegraph/.gitignore]

## Key files
- `13.3`
- `BrcmPatchRAM/BlueToolFixup.cpp`
- `Changelog.md`
- `BrcmBluetoothInjector.kext/Contents/Info.plist`
- `BrcmBluetoothInjectorLegacy.kext/Contents/Info.plist`
- `BrcmPatchRAM.xcodeproj/project.pbxproj`
- `README.md`
- `13.x`
- `.github/workflows/main.yml`
- `15.4`
- `.codegraph/.gitignore`
- `.gitignore`
- `.projectmem/PROJECT_MAP.md`
- `.projectmem/config.toml`
- `.projectmem/issues/legacy_2305-legacy-issue-bump-version-and-fixed-changelog.md`
- `.projectmem/issues/legacy_4a1a-legacy-issue-bluetoolfixup-add-another-vendor-ca.md`
- `.projectmem/issues/legacy_4f07-legacy-issue-remove-nvram-check-patch-by-default.md`
- `.projectmem/issues/legacy_5b45-legacy-issue-fix-bluetoothinternalcontrollerinfo.md`
- `.projectmem/issues/legacy_9cb4-legacy-issue-try-to-fix-ci.md`
- `.projectmem/issues/legacy_bdb3-legacy-issue-update-bluetoolfixup-for-macos-15-3.md`

## Open questions
- None logged yet.
