# wlr-hdr-calibrator

Forked from https://github.com/mischw/wl-gammactl

Made originally to get some analog to KDE Plasma 6's HDR calibration GUI, and to deal with `amdgpu`'s poor PQ EOTF behavior over HDMI, making HDR output overbrightened and generally follow a wonky EOTF curve (at least on my TVs: *TCL QM851G, TCL 55R635*).

When running `wlr-hdr-calibrator` it will kick out any running redshift instance and fail to start up. On second run it should work as expected.
So unfortunatly only one can run at a time (?) for now.

# Build
For most use cases this should do:  
Clone the repository and
```console
$ meson build
$ ninja -C build
```

# Run
To create a "LUT", run the GUI
```console
$ cd gui
$ nix-shell
$ python3 lut_editor.py
```

It will save your LUT as a plaintext file.

Call with a path to the LUT to set values, eg:
```console
$ cd build
$ wlr-hdr-calibrator ../gui/lut
```
Useful for calling on startup or in scripts (i.e. HDR toggle)


