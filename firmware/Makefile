CC      = arm-none-eabi-gcc
CFLAGS  = -mcpu=cortex-m4 -mthumb -Wall -g
LDFLAGS = -nostdlib -T stm32f429zi.ld
SRCS    = startup.c main.c

build/fw.elf: $(SRCS) stm32f429zi.ld Makefile
	mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRCS) -o $@
	arm-none-eabi-size $@

flash: build/fw.elf
	STM32_Programmer_CLI -c port=SWD -w $< -v -rst

clean:
	rm -rf build

.PHONY: flash clean
