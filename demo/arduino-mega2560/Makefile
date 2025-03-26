target := demo.elf

-include picoRTOS/Makefile.in
-include ../../Makefile.in

CC  := avr-gcc
SRC := $(c_src-y) $(a_src-y)

SRC += arduino-mega2560.c
SRC += main.c

OBJ := $(SRC:%.c=%.o)
OBJ := $(OBJ:%.S=%.o)

CFLAGS := -I. -Iinclude $(include-y) -ffunction-sections -fdata-sections
CFLAGS += -fpack-struct -fshort-enums -Wno-address -mmcu=atmega2560 -D__ATmega2560__
CFLAGS += -flto -fno-fat-lto-objects
CFLAGS += -D'__ATTRIBUTE_PACKED__=__attribute__((packed))'
CFLAGS += -D'__ATTRIBUTE_NAKED__=__attribute__((naked))'
CFLAGS += -D'__ATTRIBUTE_ALIGNED__(x)=__attribute__((aligned(x)))'
CFLAGS += -DTCLI_COMPLETE=0

AFLAGS := -Iinclude $(include-y) -x assembler-with-cpp -mmcu=atmega2560 -D__ATmega2560__ -flto
LDFLAGS := -fuse-linker-plugin -Wl,--gc-sections -mmcu=atmega2560 -D__ATmega2560__ -lm -flto

all: $(target)

$(target): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(AFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(target)
