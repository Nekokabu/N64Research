BUILD_DIR = build
include $(N64_INST)/include/n64.mk

src = memspeed.c
asm = memtest_kernel.S

all: n64_memspeed.z64

$(BUILD_DIR)/n64_memspeed.elf: $(src:%.c=$(BUILD_DIR)/%.o) $(asm:%.S=$(BUILD_DIR)/%.o)

n64_memspeed.z64: N64_ROM_TITLE = "RDRAM Test"

clean:
	rm -rf $(BUILD_DIR) n64_memspeed.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
