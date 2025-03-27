# 1st: sources & targets
target := demo_tcli.elf

c_src-y += main.c
c_src-y += stm32h7xx.c

-include ../../Makefile.in

# avoid double-inclusion
ifneq ($(skip-makefile),1)
PWD := $(shell pwd)
KBUILD_SRC := ../../../OpenPicoRTOS-dev
TTY ?= /dev/ttyACM0

all: $(target)
	@

$(target): FORCE
	$(Q)$(MAKE) -C $(KBUILD_SRC) O=$(PWD)

clean: FORCE
	$(Q)$(MAKE) -C $(KBUILD_SRC) O=$(PWD) $@
	@rm -f $(target) $(hex) *.map

%:
	$(Q)$(MAKE) -C $(KBUILD_SRC) O=$(PWD) $@

PHONY += FORCE upload
FORCE:

.PHONY: $(PHONY)

endif
