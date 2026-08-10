# Firmware and factory recovery

[简体中文](firmware_ZH.md) · [Home](../README.md)

`firmware/ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin` is an immutable
factory recovery delivery. It is inventoried and reported separately from CI;
the normal example matrix neither builds nor repackages it. Source and build
instructions for this factory image are not included in this repository yet and
may be added in a later update.

Successful example CI builds are separate diagnostic artifacts packaged by
`releases/package_firmware.py`. See [release tools](../releases/README.md) for
the archive format and download flow.
