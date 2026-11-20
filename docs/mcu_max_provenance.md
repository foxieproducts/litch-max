# mcu-max sources and permissions

Litch-Max includes a modified copy of [Gissio’s mcu-max 1.0.7](https://github.com/Gissio/mcu-max/tree/aa03caffce50729566b5db6965bf735c31f33eea). The [local source and license](../vendor/mcu-max/) are included for building and redistribution. The fork provides position access and search callbacks used by Litch-Max. Callers can supply a cooperation callback to integrate search with their own scheduler.

## License and attribution

The mcu-max code by Gissio is distributed under the [MIT license](https://github.com/Gissio/mcu-max/blob/aa03caffce50729566b5db6965bf735c31f33eea/LICENSE). Keep the copyright and permission notice with copies or substantial portions of the software.

mcu-max is based on [micro-Max 4.8](https://home.hccnet.nl/h.g.muller/umax4_8.c) by H.G. Muller. On August 22, 2026, Muller gave this permission for Micro-Max in response to a question about its use in an electronic chessboard.

> Micro-Max is public domain, so you can use it for whatever purpose you like.

The declaration names Micro-Max without a version restriction. Litch-Max records it as `LicenseRef-micro-Max-Public-Domain` for the underlying work and retains the MIT notice for mcu-max. [Author’s declaration](https://www.talkchess.com/forum/viewtopic.php?p=996683#p996683)
