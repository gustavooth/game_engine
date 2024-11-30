MAKEFLAGS += -s

ifeq ($(OS),Windows_NT)
    PLATFORM = win32
    else
        UNAME_S := $(shell uname -s)
        ifeq ($(UNAME_S),Linux)
            PLATFORM = linux
        endif
    endif

    ifeq ($(PLATFORM),win32)
        PLATFORM_MAKEFILE = Makefile.game.win32.mak
    else ifeq ($(PLATFORM),linux)
        PLATFORM_MAKEFILE = Makefile.game.linux.mak
    else
        $(error Unsupported platform: $(UNAME_S))
endif

include $(PLATFORM_MAKEFILE)
