.PHONY: release all clean

release: all
	rm -rf HxC_Compat_Mode
	mkdir -p HxC_Compat_Mode/Amiga
	mkdir -p HxC_Compat_Mode/Atari_ST
	cp amiga/AUTOBOOT.HFE HxC_Compat_Mode/Amiga
	cp atari_st/AUTOBOOT.HFE HxC_Compat_Mode/Atari_ST
	python ./mk_hxcsdfe.py HxC_Compat_Mode/HXCSDFE.CFG
	cp COPYING_PRE HxC_Compat_Mode/COPYING
	cat COPYING >>HxC_Compat_Mode/COPYING

all:
	$(MAKE) -C amiga $@
	$(MAKE) -C atari_st $@

clean:
	$(MAKE) -C amiga $@
	$(MAKE) -C atari_st $@
	rm -rf HxC_Compat_Mode
