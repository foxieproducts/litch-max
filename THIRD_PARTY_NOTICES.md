# Litch-Max third-party notices

## mcu-max and micro-Max

The mcu-max code by Gissio is distributed under the [MIT license](https://github.com/Gissio/mcu-max/blob/aa03caffce50729566b5db6965bf735c31f33eea/LICENSE). Keep the copyright and permission notice with copies or substantial portions of the software.

mcu-max is based on [micro-Max 4.8](https://home.hccnet.nl/h.g.muller/umax4_8.c) by H.G. Muller. On August 22, 2026, Muller gave this permission for Micro-Max in response to a question about its use in an electronic chessboard.

> Micro-Max is public domain, so you can use it for whatever purpose you like.

The declaration names Micro-Max without a version restriction. Litch-Max records it as `LicenseRef-micro-Max-Public-Domain` for the underlying work and retains the MIT notice for mcu-max. [Author’s declaration](https://www.talkchess.com/forum/viewtopic.php?p=996683#p996683)

The [complete MIT notice](vendor/mcu-max/LICENSE) ships with the vendored library.

## Foxie embedded utilities

The `fixed_string.hpp` and `format.hpp` headers under [third_party/foxie_embedded](third_party/foxie_embedded/) are copyright Foxie Products, LLC and distributed under [MPL 2.0](LICENSE).

## Optional generator dependency

The opening-book generator uses separately installed [python-chess](https://python-chess.readthedocs.io/en/latest/#license) under GPL-3.0-or-later for generation and validation. The C++ library and firmware do not link or bundle python-chess.
