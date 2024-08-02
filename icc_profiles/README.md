# ICC Profiles

This directory contains precomputed ICC profiles embedded as byte arrays in C++ header files. These headers can be used by Lilliput to ensure color profiles are consistently available without the need for runtime file I/O. This is particularly useful for images extracted from video, which reference a color-space but do not include an ICC profile.

## Generating ICC Profile Headers

To generate the ICC profile headers, you can use the provided Python script. This script reads ICC profile files and converts them into C-style byte arrays, which are then saved as header files in this directory.

### Requirements

- Python 3.x

### Profiles

The following ICC profiles are included:
- [sRGB](https://github.com/saucecontrol/Compact-ICC-Profiles/blob/master/profiles/sRGB-v4.icc)
- [Rec709](https://github.com/saucecontrol/Compact-ICC-Profiles/blob/master/profiles/Rec709-v4.icc)
- [Rec2020](https://github.com/saucecontrol/Compact-ICC-Profiles/blob/master/profiles/Rec2020Compat-v4.icc)
- [Rec601 PAL](https://github.com/saucecontrol/Compact-ICC-Profiles/blob/master/profiles/Rec601PAL-v4.icc)
- [Rec601 NTSC](https://github.com/saucecontrol/Compact-ICC-Profiles/blob/master/profiles/Rec601NTSC-v4.icc)

### Script Usage

1. **Prepare the ICC Profile Files**: Ensure you have the ICC profile files (`.icc`) available in the same directory as the script or provide the correct path to each file.

2. **Run the Script**: Use the provided Python script to generate the header files: 
```python3 icc_profile.py```
