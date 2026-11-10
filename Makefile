# -----------------------------------------------------------------------------
# EGN engine + game
#
# Single Makefile for the whole project.
#
# Targets:
#   make / make all   build the engine and the game
#   make engine       build only the engine (library + shaders)
#   make game         build only the game (engine must be built first)
#   make run          build everything and run the game
#   make clean        remove build/ and bin/
#   make v            clone/build V into third_part/v
#   make help         show targets/variables
#
# Platform selection:
#   PLATFORM is detected automatically; override with `make PLATFORM=linux`.
#   Add new platforms by adding an `else ifeq ($(PLATFORM),...)` block below.
# -----------------------------------------------------------------------------

.DEFAULT_GOAL := all
.NOTPARALLEL: all

# -----------------------------------------------------------------------------
# Platform detection and platform-specific settings
# -----------------------------------------------------------------------------

ifeq ($(origin PLATFORM), undefined)
  ifeq ($(OS),Windows_NT)
    PLATFORM := windows
  else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
      PLATFORM := linux
    else ifeq ($(UNAME_S),Darwin)
      PLATFORM := macos
    else
      PLATFORM := $(shell echo $(UNAME_S) | tr '[:upper:]' '[:lower:]')
    endif
  endif
endif

ifeq ($(PLATFORM),linux)
  PLATFORM_CFLAGS := -fPIC
  PLATFORM_LDFLAGS := -shared
  PLATFORM_LIBS := -lwayland-client -lvulkan -lm
  PLATFORM_BACKEND := wayland
  PLATFORM_INC :=
  ENGINE_LIB_NAME := libengine.so
  GAME_EXE_NAME := game
  MKDIR := mkdir -p
  COPY := cp
  CLEAN_CMD := rm -rf build bin
  RUN_CMD := cd bin && exec ./$(GAME_EXE_NAME)
# Add future platforms here, for example:
# else ifeq ($(PLATFORM),windows)
#   PLATFORM_CFLAGS :=
#   PLATFORM_LDFLAGS := -shared
#   PLATFORM_LIBS := -lvulkan-1 -lm
#   PLATFORM_BACKEND := win32
#   PLATFORM_INC := -I"$(VULKAN_SDK)/Include"
#   ENGINE_LIB_NAME := engine.dll
#   GAME_EXE_NAME := game.exe
#   MKDIR := mkdir
#   COPY := copy /Y
#   CLEAN_CMD := rmdir /s /q build bin
#   RUN_CMD := cd bin && $(GAME_EXE_NAME)
else
  $(error Unsupported platform '$(PLATFORM)'. Add a platform block to the Makefile.)
endif

# -----------------------------------------------------------------------------
# Tools and paths
# -----------------------------------------------------------------------------

ifeq ($(origin CC),default)
  CC := clang
endif
GLSLANG ?= glslc
V ?= $(if $(wildcard third_part/v/v),third_part/v/v,v)
V_FLAGS ?=

ENGINE_DIR := engine
GAME_DIR := game
THIRD_PARTY_DIR := third_part

BUILD_DIR := build
BIN_DIR := bin

ENGINE_LIB := $(BIN_DIR)/$(ENGINE_LIB_NAME)
GAME_BIN := $(BIN_DIR)/$(GAME_EXE_NAME)

# -----------------------------------------------------------------------------
# Compiler flags
# -----------------------------------------------------------------------------

C_FLAGS := -g -MD -Wall -Werror -Wvarargs -Wno-missing-braces -Werror=vla
DEFINES := -DEGNEXPORT
INC_FLAGS := -I$(ENGINE_DIR) -I$(THIRD_PARTY_DIR) $(PLATFORM_INC)

# -----------------------------------------------------------------------------
# Engine sources
# -----------------------------------------------------------------------------

# Common engine sources, excluding platform-specific directories.
# Platform files are added back below from engine/platform/<os> and the
# Vulkan WSI backend file.
ENGINE_CORE_SRC := $(shell find $(ENGINE_DIR) -name '*.c' \
	! -path '$(ENGINE_DIR)/platform/*/*' \
	! -path '$(ENGINE_DIR)/renderer/vulkan/platform/*')

ENGINE_PLATFORM_SRC := $(shell find $(ENGINE_DIR)/platform/$(PLATFORM) -name '*.c' 2>/dev/null)
ENGINE_BACKEND_SRC := $(wildcard $(ENGINE_DIR)/renderer/vulkan/platform/*_$(PLATFORM_BACKEND).c)

ENGINE_SRC := $(ENGINE_CORE_SRC) $(ENGINE_PLATFORM_SRC) $(ENGINE_BACKEND_SRC)
ENGINE_OBJ := $(patsubst $(ENGINE_DIR)/%.c,$(BUILD_DIR)/$(ENGINE_DIR)/%.o,$(ENGINE_SRC))
ENGINE_DEP := $(ENGINE_OBJ:.o=.d)

VERT_SRC := $(shell find $(ENGINE_DIR) -name '*.vert')
FRAG_SRC := $(shell find $(ENGINE_DIR) -name '*.frag')
SPIRV := $(patsubst $(ENGINE_DIR)/%.vert,$(BIN_DIR)/%.vert.spv,$(VERT_SRC)) \
         $(patsubst $(ENGINE_DIR)/%.frag,$(BIN_DIR)/%.frag.spv,$(FRAG_SRC))

# -----------------------------------------------------------------------------
# Game sources
# -----------------------------------------------------------------------------

GAME_V_SRC := $(shell find $(GAME_DIR) -name '*.v')
SDK_V_SRC := $(shell find egn -name '*.v')
GAME_MANIFEST := $(GAME_DIR)/v.mod
SDK_MANIFEST := egn/v.mod

GAME_ASSETS_SRC := $(shell find $(GAME_DIR)/assets -type f 2>/dev/null)
GAME_ASSETS := $(patsubst $(GAME_DIR)/assets/%,$(BIN_DIR)/assets/%,$(GAME_ASSETS_SRC))

# `game/v.mod` is the V project root. Add the repository root so `import egn`
# resolves to <repo>/egn. @vlib/@vmodules keep the standard modules available.
V_PATH := $(abspath $(GAME_DIR)/..)|@vlib|@vmodules

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

.PHONY: all build engine game run clean v help

all: engine game

build: all

engine: $(ENGINE_LIB) $(SPIRV)

game: $(GAME_BIN)

$(ENGINE_LIB): $(ENGINE_OBJ)
	@echo "  LD   $@"
	@$(MKDIR) $(dir $@)
	@$(CC) $(PLATFORM_LDFLAGS) $(ENGINE_OBJ) $(PLATFORM_LIBS) -o $@

$(BUILD_DIR)/$(ENGINE_DIR)/%.o: $(ENGINE_DIR)/%.c
	@echo "  CC   $<"
	@$(MKDIR) $(dir $@)
	@$(CC) $(C_FLAGS) $(PLATFORM_CFLAGS) $(DEFINES) $(INC_FLAGS) -c $< -o $@

$(BIN_DIR)/%.spv: $(ENGINE_DIR)/%
	@echo "  SPV  $<"
	@$(MKDIR) $(dir $@)
	@$(GLSLANG) $< -o $@

$(GAME_BIN): $(GAME_V_SRC) $(SDK_V_SRC) $(GAME_MANIFEST) $(SDK_MANIFEST) $(GAME_ASSETS)
	@if [ ! -f $(ENGINE_LIB) ]; then \
		echo "error: $(ENGINE_LIB) not found; run 'make engine' first"; \
		exit 1; \
	fi
	@echo "  V    $@"
	@$(MKDIR) $(dir $@)
	@$(V) $(V_FLAGS) -path '$(V_PATH)' -o $@ $(GAME_DIR)

$(BIN_DIR)/assets/%: $(GAME_DIR)/assets/%
	@echo "  COPY $<"
	@$(MKDIR) $(dir $@)
	@$(COPY) $< $@

run: all
	@$(RUN_CMD)

clean:
	@echo "  CLEAN build/ bin/"
	@$(CLEAN_CMD)

v:
	@if [ ! -x third_part/v/v ]; then \
		git clone --depth 1 https://github.com/vlang/v third_part/v; \
		$(MAKE) -C third_part/v; \
	else \
		echo "V already built at third_part/v/v"; \
	fi

help:
	@echo "EGN build targets:"
	@echo "  make / make all   build engine + game"
	@echo "  make engine       build only the engine (library + shaders)"
	@echo "  make game         build only the game (requires the engine)"
	@echo "  make build        alias for 'make all'"
	@echo "  make run          build everything and run the game"
	@echo "  make clean        remove build/ and bin/"
	@echo "  make v            clone/build V in third_part/v"
	@echo "  make help         show this help"
	@echo ""
	@echo "Variables:"
	@echo "  PLATFORM=$(PLATFORM)  CC=$(CC)  V=$(V)"
	@echo "  Override: make PLATFORM=linux CC=clang V=/path/to/v"

-include $(ENGINE_DEP)
