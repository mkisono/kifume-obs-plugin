# Kifu.me Plugin for OBS

OBS plugin used by kifu.me to display dice overlays in OBS.
This plugin works together with kifume-inference, which detects dice from captured images.

## Template Notice

This repository was created from obs-plugintemplate:
https://github.com/obsproject/obs-plugintemplate

## Build (Windows)

Configure:

	cmake --preset windows-x64

Build:

	cmake --build build_x64 --config RelWithDebInfo --target kifume-plugin-for-obs

Install:

	cmake --install build_x64 --config RelWithDebInfo

## Distribution and Support

- User-facing plugin/inference downloads are published from kifume-support Releases.
- User documentation is published on kifume-support Pages.

Support repository:
https://github.com/mkisono/kifume-support

OBS plugin docs:
https://docs.kifu.me/OBS_PLUGIN.html

## License

See LICENSE.
