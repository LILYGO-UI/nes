# FCEUmm Source

Managed as a Git submodule at `third_party/fceumm` from the official Libretro
FCEUmm repository:

- Repository: https://github.com/libretro/libretro-fceumm.git
- Commit: `236ccdfc911e84c60fea6b9d0699c2d440a8de14`

Initialize the revision pinned by the parent repository from its root:

```sh
git submodule update --init --recursive
```

The complete upstream working tree is retained without local source changes.
The application builds the core as a static library through
`cmake/Fceumm.cmake` and uses the Libretro C API.

The build uses native RGB565 output. Optional analog NTSC filtering and HD texture
packs are not enabled in the application build; their upstream sources remain
in the submodule. This does not remove cartridge mapper support. All
libretro-common functions needed by the core are linked locally, since the
application does not supply the RetroArch frontend's helper symbols.

This integration note is maintained in the parent repository as
`third_party/FCEUMM_ORIGIN.md` and installed with the package as
`share/doc/lilygo-ui-nes/fceumm/ORIGIN.md`.

FCEUmm's source notices specify GNU GPL version 2 or later. `Copying` contains the
complete upstream GPL version 2 text. Individual files, including libretro-common
and the sound-chip implementations, retain their own copyright and license
notices. Preserve these notices and the license when redistributing source or
binaries containing the core.
