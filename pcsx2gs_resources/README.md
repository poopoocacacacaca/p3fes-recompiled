# PCSX2 GS resources

Copied from PCSX2's `bin/resources` (github.com/PCSX2/pcsx2, commit `2954ebe6965669219f1bbd256e958fbd8f83a85d`).
The build copies this folder next to the `pcsx2gs` library as `pcsx2gs_resources/`.

- `shaders/opengl/`: GLSL shaders of the OpenGL device (compiled at run time). GPL-3.0, like PCSX2.
- `shaders/common/`: `fxaa.fx` (FXAA, GPL-3.0 in PCSX2) and `ffx_a.h`/`ffx_cas.h` (AMD FidelityFX CAS, MIT licence in
  each file's header).
- `fonts/Roboto-Regular.ttf`: OSD font (Apache 2.0, see `fonts/Roboto-Regular-copyright`).
