# HxC File Selector for FlashFloppy

A modified version of the HxC project's file selector/manager for
enhanced compatibility with FlashFloppy on Amiga and Atari ST. The
FlashFloppy project should work with unmodified original HxC
selectors (AUTOBOOT.HFE) on other platforms (notably Amstrad CPC).

This software is licensed under GPLv3. See
[COPYING_FULL](/COPYING_FULL) for full copyright and license
information.

This project is cross-compiled on an x86 Ubuntu Linux system. However
other similar Linux-base systems (or a Linux virtual environment on
another OS) can likely be made to work quite easily.

## Building for Amiga

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

Building the ADF image from the selector executable requires
bonefish's exe2adf on your path. Download it
[here](http://www.exe2adf.com).

To produce the final HFE images from ADF requires disk-analyse on your
path. Download and build it:
```
 # git clone https://github.com/keirf/Disk-Utilities.git
 # cd Disk-Utilities
 # make
 # make install
```

To build the HFE image:
```
 # git clone https://github.com/keirf/HxC_FF_File_Selector.git
 # cd HxC_FF_File_Selector/amiga
 # make
```

## Building for Atari ST

You must build and install the m68k-atari-mint cross compiler, maintained
by Vincent Riviere. The simplest method is to use Miro Kropacek's automated
build scripts:
```
 # mkdir atari-mint
 # cd atari-mint
 # mkdir install
 # git clone https://github.com/mikrosk/m68k-atari-mint-build
 # cd m68k-atari-mint-build
 # INSTALL_DIR="$HOME/atari-mint/install" make m68000-skip-native
```

You must also build and install vasm (copy the `vasmm68k_mot`
executable onto a PATH location):
```
 # wget http://sun.hasenbraten.de/vasm/release/vasm.tar.gz
 # tar xf vasm.tar.gz
 # cd vasm
 # make CPU=m68k SYNTAX=mot
```

The atari-mint tools must be on your PATH when you build the selector
software:
```
 # export PATH=/home/<username>/atari-mint/install/m68000/bin
```

To build the HFE image (requires disk-analyse installed, as described
in the Amiga build instructions above):
```
 # git clone https://github.com/keirf/HxC_FF_File_Selector.git
 # cd HxC_FF_File_Selector/atari_st
 # make
```

## Building All Targets

Install prerequisites as above. Then:
```
 # git clone https://github.com/keirf/HxC_FF_File_Selector.git
 # cd HxC_FF_File_Selector
 # make
```
