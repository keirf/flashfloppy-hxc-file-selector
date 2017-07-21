# FlashFloppy File Selector

A modified version of the HxC project's file selector/manager for
Amiga systems.  The FlashFloppy project should work with unmodified
original HxC selectors (AUTOBOOT.HFE) on other platforms.

This software is licensed under GPLv3. See
[COPYING_FULL](/COPYING_FULL) for full copyright and license
information.

## Building

This project is cross-compiled on an x86 Ubuntu Linux system. However
other similar Linux-base systems (or a Linux virtual environment on
another OS) can likely be made to work quite easily.

You must build and install bebbo's GCC v6 port for Amiga. This can be
done to a private path in your home directory, for example:
```
 # mkdir bebbo
 # cd bebbo
 # mkdir install
 # git clone https://github.com/bebbo/amigaos-cross-toolchain.git repo
 # cd repo
 # ./toolchain-m68k --prefix=/home/<username>/bebbo/install build
```

The compiler must be on your PATH when you build the selector software:
```
 # export PATH=/home/<username>/bebbo/install/bin:$PATH
```

To build AUTOBOOT.HFE:
```
 # git clone https://github.com/keirf/FlashFloppy_File_Selector.git
 # cd FlashFloppy_File_Selector/amiga
 # make
```
