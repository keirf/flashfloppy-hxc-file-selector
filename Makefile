all:
	$(MAKE) -C amiga $@
	$(MAKE) -C atari_st $@

clean:
	$(MAKE) -C amiga $@
	$(MAKE) -C atari_st $@
