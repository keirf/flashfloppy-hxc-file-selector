
# HxC File Selector for FlashFloppy

![CI Badge][ci-badge]
![Version Badge][version-badge]

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

This project uses exe2adf written by Bonefish/Reality. For
convenience I have included the Linux 64-bit binary within this
repository. You can find the original download on Bonefish's
[exe2adf website](http://www.exe2adf.com). Check it out!

## Building for Amiga

You must build and install bebbo's GCC v6 port for Amiga.  GCC has
prerequisites that are listed in the
[README](https://github.com/bebbo/amiga-gcc/blob/master/README.md),
along with instructions on how to install them in various
environments.  The build can be done to a private path in your home
directory, for example:
```
 # cd $HOME
 # mkdir -p install
 # git clone https://github.com/bebbo/amiga-gcc
 # cd amiga-gcc ; make update
 # make all -j8 PREFIX=$HOME/install
```

To produce the final HFE images from ADF requires disk-analyse. Download
and build it:
```
 # git clone https://github.com/keirf/disk-utilities.git
 # cd disk-utilities
 # PREFIX=$HOME/install make && make install
```

The compiler and disk-analyse must be on your PATH when you build
the selector software:
```
 # export PATH=$HOME/install/bin:$PATH
 # export LD_LIBRARY_PATH=$HOME/install/lib:$LD_LIBRARY_PATH
```

To build the HFE image:
```
 # git clone https://github.com/keirf/flashfloppy-hxc-file-selector.git
 # cd flashfloppy-hxc-file-selector/amiga
 # make
```

## Building for Atari ST

This build requires the m68k-atari-mint cross compiler. On Ubuntu you
can install Vincent Riviere's pre-built packages:
```
 # sudo add-apt-repository ppa:vriviere/ppa
 # sudo apt update
 # sudo apt install cross-mint-essential
```

In other environments, or if a package is unavailable for your version
of Ubuntu, then you must build the cross compiler from source. The
simplest method is to use Miro Kropacek's automated build scripts into
a private path in your home directory:
```
 # cd $HOME
 # mkdir -p install
 # git clone https://github.com/mikrosk/m68k-atari-mint-build
 # cd m68k-atari-mint-build
 # git checkout 1fd98b25ceb
 # INSTALL_DIR="$HOME/install" make m68000-skip-native
```

The compiler and disk-analyse (see Amiga build instructions above) must be
on your PATH when you build the selector software:
```
 # export PATH=$HOME/install/bin:$PATH
 # export LD_LIBRARY_PATH=$HOME/install/lib:$LD_LIBRARY_PATH
```

To build the HFE image:
```
 # git clone https://github.com/keirf/flashfloppy-hxc-file-selector.git
 # cd flashfloppy-hxc-file-selector/atari_st
 # make
```

## Building All Targets

Install prerequisites as above. Then:
```
 # git clone https://github.com/keirf/flashfloppy-hxc-file-selector.git
 # cd flashfloppy-hxc-file-selector
 # make
```

[ci-badge]: https://github.com/keirf/flashfloppy-hxc-file-selector/workflows/CI/badge.svg
[version-badge]: https://img.shields.io/github/v/release/keirf/flashfloppy-hxc-file-selector
