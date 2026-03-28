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

ifeq ($(DEBUG),FALSE)
	GCCFLAGS += -Os
else
	GCCFLAGS += -O0 -g
endif

OBJCOPY = arm-none-eabi-objcopy
OBJS = $(patsubst ./%.c,$(DISTDIR)/%.o,$(shell find . -name \*.c))
OBJS += $(patsubst ./%.cpp,$(DISTDIR)/%.o,$(shell find . -name \*.cpp))
OBJS += $(patsubst ./%.S,$(DISTDIR)/%.o,$(shell find . -name \*.S))
OBJS += $(DISTDIR)/song.o
EXE = test
DISTDIR = build
CALC_DEST = /
vpath %.tns $(DISTDIR)
vpath %.elf $(DISTDIR)

.PHONY: all clean deploy run

all: $(DISTDIR)/$(EXE).tns

$(DISTDIR)/%.o: %.c | $(DISTDIR)
	mkdir -p $(dir $@)
	$(GCC) $(GCCFLAGS) -c $< -o $@

$(DISTDIR)/%.o: %.cpp | $(DISTDIR)
	mkdir -p $(dir $@)
	$(GXX) $(GCCFLAGS) -c $< -o $@
	
$(DISTDIR)/%.o: %.S | $(DISTDIR)
	mkdir -p $(dir $@)
	$(AS) -c $< -o $@

$(DISTDIR)/song.o: song.bin | $(DISTDIR)
	$(OBJCOPY) -I binary -O elf32-littlearm -B arm $< $@

$(DISTDIR)/$(EXE).elf: $(OBJS) | $(DISTDIR)
	$(LD) $^ -o $@ $(LDFLAGS)

$(DISTDIR)/$(EXE).tns: $(DISTDIR)/$(EXE).elf | $(DISTDIR)
	$(GENZEHN) --input $^ --output $@.zehn $(ZEHNFLAGS)
	make-prg $@.zehn $@
	rm $@.zehn

$(DISTDIR):
	mkdir -p $(DISTDIR)

clean:
	rm -f $(OBJS) $(DISTDIR)/$(EXE).tns $(DISTDIR)/$(EXE).elf $(DISTDIR)/$(EXE).tns.zehn

deploy: $(DISTDIR)/$(EXE).tns
	$(NSPIRECTL) send $< $(CALC_DEST)

run: deploy
	@echo "Uploaded $(DISTDIR)/$(EXE).tns to $(CALC_DEST) on calculator."
	@echo "Open it on the calculator to run."
