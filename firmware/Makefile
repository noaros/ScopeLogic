CC      = arm-none-eabi-gcc
CFLAGS  = -mcpu=cortex-m4 -mthumb -Wall -g
LDFLAGS = -nostdlib -T stm32f429zi.ld
SRCS    = startup.c main.c
# make MODE=2 FS=500000 LOAD=0 WAIT=1 builds with those instead of the values in main.c
DEFS    = $(if $(MODE),-DMODE=$(MODE)) $(if $(FS),-DFS=$(FS)u) $(if $(LOAD),-DLOAD=$(LOAD)) $(if $(WAIT),-DDMA_WAIT=$(WAIT))

build/fw.elf: $(SRCS) stm32f429zi.ld Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(DEFS) $(LDFLAGS) $(SRCS) -o $@
	arm-none-eabi-size $@

flash: build/fw.elf
	STM32_Programmer_CLI -c port=SWD -w $< -v -rst

clean:
	rm -rf build

# build/fw.elf is phony so it's always rebuilt: MODE and FS may have changed
.PHONY: build/fw.elf flash clean
