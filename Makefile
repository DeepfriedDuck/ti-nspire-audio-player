DEBUG = FALSE

GCC = nspire-gcc
AS  = nspire-as
GXX = nspire-g++
LD  = nspire-ld
GENZEHN = genzehn
NSPIRECTL = nspirectl

GCCFLAGS = -Wall -W -marm
LDFLAGS =
ZEHNFLAGS = --name ""

SRC_DIR = src
BUILD_DIR = build
EXE = test
CALC_DEST = /

ifeq ($(DEBUG),FALSE)
	GCCFLAGS += -Os
else
	GCCFLAGS += -O0 -g
endif

SRCS_C   = $(shell find $(SRC_DIR) -name \*.c)
SRCS_CPP = $(shell find $(SRC_DIR) -name \*.cpp)
SRCS_S   = $(shell find $(SRC_DIR) -name \*.S)

OBJS  = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS_C))
OBJS += $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SRCS_CPP))
OBJS += $(patsubst $(SRC_DIR)/%.S,$(BUILD_DIR)/%.o,$(SRCS_S))

vpath %.tns $(BUILD_DIR)
vpath %.elf $(BUILD_DIR)

.PHONY: all clean deploy run

all: $(BUILD_DIR)/$(EXE).tns

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(GCC) $(GCCFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(GXX) $(GCCFLAGS) -c $< -o $@
	
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.S | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(AS) -c $< -o $@

$(BUILD_DIR)/$(EXE).elf: $(OBJS) | $(BUILD_DIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/$(EXE).tns: $(BUILD_DIR)/$(EXE).elf | $(BUILD_DIR)
	$(GENZEHN) --input $^ --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm $@.zehn

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -f $(OBJS) $(BUILD_DIR)/$(EXE).tns $(BUILD_DIR)/$(EXE).elf $(BUILD_DIR)/$(EXE).tns.zehn

deploy: $(BUILD_DIR)/$(EXE).tns
	$(NSPIRECTL) send $< $(CALC_DEST)

run: deploy
	@echo "Uploaded $(BUILD_DIR)/$(EXE).tns to $(CALC_DEST) on calculator."
	@echo "Open it on the calculator to run."
