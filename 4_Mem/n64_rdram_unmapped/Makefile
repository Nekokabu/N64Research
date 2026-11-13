BUILD_DIR = build
include $(N64_INST)/include/n64.mk

src = rdram_unmapped.c

all: rdram_unmapped.z64

$(BUILD_DIR)/rdram_unmapped.elf: $(src:%.c=$(BUILD_DIR)/%.o)

rdram_unmapped.z64: N64_ROM_TITLE = "RDRAM Unmapped Rd"

clean:
	rm -rf $(BUILD_DIR) rdram_unmapped.z64

-include $(wildcard $(BUILD_DIR)/*.d)

.PHONY: all clean
