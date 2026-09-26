# Config Backup, Restore and Transfer (OT-Dev)

This Preferences page saves an `.otconfig` file using the existing uncompressed
ZIP writer. It backs up personal preferences, shortcuts, room sets, menus, user
settings, selected config files, FX presets and registered environment values.
The user may also include the OpenToonz library (including MyPaint brushes and
brushes found in standard MyPaint data locations) and the plugin folder.

## Restore behavior

Load Configuration previews each archive file before staging anything. Existing
library and plugin files remain unchecked. The archive suggests which copy is
newer only when both sides have comparable package version metadata; file dates
alone do not establish a version. Plugin packages from another OS or CPU
architecture cannot be selected. Same-platform plugins still require the user
to check OpenToonz API compatibility before selecting them.

Recognized preference keys are merged with the destination's current INI;
unrecognized keys and machine-specific paths are skipped. Shortcut commands
that do not exist in the receiving build are skipped; room commands belonging
to restored layouts are kept for registration after restart. Environment
variables are limited to names registered by that build, and values resembling
machine paths stay local. Room INIs receive basic syntax and pane-sequence
validation;
unavailable panel types can fall back to OpenToonz's generic panel behavior.
Room XML and selected user XML files receive syntax validation. A changed room
set must be selected as a unit so its list, layouts and menus stay together.

Selected files are staged under the current config root and installed **at the
next launch**, before preferences, rooms and plugins load. Every destination is
backed up before changes begin. If installation fails or stops unexpectedly,
the startup path restores the backups and retains the staged files in a failed
or interrupted folder for diagnosis.

The ZIP reader supports UTF-8, uncompressed (method 0), classic ZIP archives.
This format has a 4 GiB archive limit; individual files are limited to 512 MiB
by the restore workflow. Scene/project files, cache, crash logs, installation
permissions and version-control credentials are outside the default backup.
The archive preserves user configuration; it does not verify that every scene
dependency is present, nor does it validate a plugin's OpenToonz ABI.

## Review and testing

1. Modify preferences, shortcuts, two named rooms and a custom brush. Export,
   inspect the ZIP manifest, then restore to a separate portable installation.
2. Transfer between two usernames and a different monitor arrangement. Confirm
   room directories are remapped and main-window/popup geometry stays local.
3. Restore an archive with removed preference/shortcut IDs. Confirm the import
   omits them while retaining destination-only settings.
4. Put an older and a newer versioned plugin file in source/destination.
   Confirm the preview recommends the newer copy but does not check either
   existing plugin by default. Simulate an installation failure and verify
   that the backup copy restores the installed file.
5. Alter a ZIP entry, manifest hash, filename, or room INI. Confirm it is
   rejected without changing the active configuration. Interrupt installation
   after the transaction marker and verify next-launch rollback.

This feature was developed with AI assistance (OpenAI Codex, GPT-6). The code
uses the repository's existing ZIP writer and Qt/OpenToonz APIs; no new bundled
third-party library or asset is introduced.
