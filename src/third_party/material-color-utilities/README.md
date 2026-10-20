Subset of Google's Material Color Utilities (C++), Apache-2.0 (see LICENSE):
https://github.com/material-foundation/material-color-utilities, `cpp/` at
bd811e4ca2ab94c4ce3cadc89c19b1820b9704b3.

Only what `src/materialyou.cpp` needs to build the Tonal Spot scheme from a seed
colour: cam, palettes, dynamiccolor, scheme/scheme_tonal_spot, contrast, dislike,
blend, utils. Image quantization, scoring and temperature are left out. The
schemes follow the 2021 spec of this C++ port; kde-material-you-colors defaults
to the 2025 spec, so the exact tones differ slightly from its output.

Local change: `utils/utils.cc` formats `HexFromArgb` with `snprintf` instead of
abseil, the only abseil use in the subset. To update, copy the same files from a
newer commit and keep that change.
