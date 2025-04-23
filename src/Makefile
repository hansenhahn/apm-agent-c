include ../defines.mk

OUTDIR:=../out

SRCS=$(wildcard *.c) $(wildcard contrib/*.c)
OBJS=$(patsubst %.c,$(OUTDIR)/%.o,$(SRCS))

STATIC_LIBRARY=$(OUTDIR)/$(LIBNAME).a
DYNAMIC_LIBRARY=$(OUTDIR)/$(LIBNAME).so

# Linker options
LDFLAGS=-shared \
	-ltrrmap \
	-ltrrlog \
	-ltrrutil \
	-lz \
	-lcurl \
	-pthread \
	-Wl,-soname,$(LIBNAME).so.$(MAJOR)

# Compiler flags
CFLAGSEX:=-I../include/ -fPIC \
	-std=c99 \
	-Wno-deprecated-declarations \
	-Wall \
	-Wextra \
	-Wundef \
	-Wpointer-arith \
	-Wshadow \
	-Wstrict-prototypes \
	-Wunreachable-code \
	-D_GNU_SOURCE 
ifdef BUILD_APM_WITH_LIBCURL
CFLAGSEX+=-D__BUILD_APM_WITH_LIBCURL
endif
ifdef BUILD_APM_WITH_LIBTRRVM
CFLAGSEX+=-D__BUILD_APM_WITH_LIBTRRVM
endif
ifdef BUILD_APM_WITH_LIBWSCLIENT
CFLAGSEX+=-D__BUILD_APM_WITH_LIBWSCLIENT
endif
ifdef BUILD_APM_WITH_LIBTRRMANAGER
CFLAGSEX+=-D__BUILD_APM_WITH_LIBTRRMANAGER
endif
ifdef BUILD_APM_WITH_LIBTRRVMCOMM
CFLAGSEX+=-D__BUILD_APM_WITH_LIBTRRVMCOMM
endif

ifeq ($(DEBUG), 1)
	CFLAGSEX+=-g
else
endif

all: static dynamic

static: $(STATIC_LIBRARY)

dynamic: $(DYNAMIC_LIBRARY)

$(STATIC_LIBRARY): $(OBJS)
	$(call print,$(PURPLE),"Creating static library $@")
	ar rcs $@ $^

$(DYNAMIC_LIBRARY): $(OBJS)
	$(call print,$(PURPLE),"Creating dynamic library $@")
	$(CC) $(LDFLAGS) -o $@ $^

$(OUTDIR)/%.o: %.c
	$(call print,$(GREEN),"Compiling $< into $@")
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CFLAGSEX) -c $< -o $@

.PHONY: clean

clean:
	$(call print,$(RED),"Cleaning up...")
	rm -f $(OBJS)
	rm -rf $(STATIC_LIBRARY)
	rm -rf $(DYNAMIC_LIBRARY)