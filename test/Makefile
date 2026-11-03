# SPDX-License-Identifier: MIT
# PC test: runs the firmware's C modules against a simulated nRF52832.
#   make        build and run all tests
FW      := ../firmware/nrf52_swd_programmer
CC      ?= gcc
CFLAGS  := -std=c99 -O2 -Wall -Wextra -Werror -DPROGRAMMER_SIM -I$(FW) -I.
SRCS    := $(FW)/swd.c $(FW)/nrf52_target.c $(FW)/ihex.c $(FW)/programmer.c \
           sim_target.c sim_host.c test_main.c
BUILD   := build

all: test

$(BUILD)/test_programmer: $(SRCS) $(wildcard $(FW)/*.h) $(wildcard *.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $(SRCS)

$(BUILD)/data/good.hex: make_test_hex.py
	python3 make_test_hex.py $(BUILD)/data

test: $(BUILD)/test_programmer $(BUILD)/data/good.hex
	./$(BUILD)/test_programmer $(BUILD)/data

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
