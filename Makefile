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
#   make clean        remove build/ and output/
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
  PLATFORM_LIBS := -lwayland-client -lvulkan -lm -lpthread
  PLATFORM_BACKEND := wayland
  PLATFORM_INC :=
  ENGINE_LIB_NAME := libengine.so
  GAME_EXE_NAME := game
  MKDIR := mkdir -p
  COPY := cp
  CLEAN_CMD := rm -rf build output
  RUN_CMD := cd output/linux && exec ./$(GAME_EXE_NAME)
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
#   CLEAN_CMD := rmdir /s /q build output
#   RUN_CMD := cd output\linux && $(GAME_EXE_NAME)
else
  $(error Unsupported platform '$(PLATFORM)'. Add a platform block to the Makefile.)
endif

# -----------------------------------------------------------------------------
# Tools and paths
# -----------------------------------------------------------------------------

ifeq ($(origin CC),default)
  CC := clang
endif
ifeq ($(origin CXX),default)
  CXX := g++
endif
GLSLANG ?= glslc
CMAKE ?= $(if $(wildcard third_part/cmake/cmake/data/bin/cmake),third_part/cmake/cmake/data/bin/cmake,cmake)
V ?= $(if $(wildcard third_part/v/v),third_part/v/v,v)
V_FLAGS ?=

ENGINE_DIR := engine
GAME_DIR := game
THIRD_PARTY_DIR := third_part

JOLTC_DIR := $(THIRD_PARTY_DIR)/JoltC
JOLT_DIR := $(THIRD_PARTY_DIR)/JoltPhysics
JOLT_LIBS := $(JOLTC_DIR)/build/lib/libjoltc.a $(JOLTC_DIR)/build/lib/libJolt.a
JOLT_STAMP := $(JOLTC_DIR)/build/.egn_jolt_built
JOLT_INC := -I$(JOLTC_DIR)/include

# WebAssembly/WebGPU toolchain (Emscripten SDK installed under third_part).
EMSDK_DIR ?= $(THIRD_PARTY_DIR)/emsdk
EMCC ?= $(EMSDK_DIR)/upstream/emscripten/emcc
EMXX ?= $(EMSDK_DIR)/upstream/emscripten/em++
EMCMAKE ?= $(EMSDK_DIR)/upstream/emscripten/emcmake
JOLTC_WEB_DIR := $(JOLTC_DIR)/build-web
JOLT_WEB_LIBS := $(JOLTC_WEB_DIR)/lib/libjoltc.a $(JOLTC_WEB_DIR)/lib/libJolt.a
JOLT_WEB_STAMP := $(JOLTC_WEB_DIR)/.egn_jolt_web_built

WEB_DIR := web
OUTPUT_DIR := output
OUTPUT_LINUX := $(OUTPUT_DIR)/linux
OUTPUT_WEB := $(OUTPUT_DIR)/web
WEB_DIST := $(OUTPUT_WEB)
WEB_ENGINE_JS := $(WEB_DIST)/engine.js
WEB_ENGINE_WASM := $(WEB_DIST)/engine.wasm
WEB_BUILD_DIR := build/web
WEB_EXPORTS := $(WEB_BUILD_DIR)/exports.txt
WEB_OPEN ?= xdg-open
WEB_BROWSER ?= $(shell command -v chromium 2>/dev/null || command -v google-chrome-stable 2>/dev/null || command -v google-chrome 2>/dev/null || command -v chromium-browser 2>/dev/null)
WEB_BROWSER_FLAGS ?= --allow-file-access-from-files --enable-unsafe-webgpu --enable-features=Vulkan --use-angle=vulkan --ignore-gpu-blocklist --disable-gpu-sandbox --enable-dawn-features=allow_unsafe_apis,disable_adapter_blocklist --disable-dawn-features=disallow_unsafe_apis
WEB_PROFILE ?= $(HOME)/.cache/egn-web-chromium

BUILD_DIR := build/linux

ENGINE_LIB := $(OUTPUT_LINUX)/$(ENGINE_LIB_NAME)
GAME_BIN := $(OUTPUT_LINUX)/$(GAME_EXE_NAME)

# -----------------------------------------------------------------------------
# Compiler flags
# -----------------------------------------------------------------------------

C_FLAGS := -g -MD -Wall -Werror -Wvarargs -Wno-missing-braces -Werror=vla
DEFINES := -DEGNEXPORT
INC_FLAGS := -I$(ENGINE_DIR) -I$(THIRD_PARTY_DIR) $(JOLT_INC) $(PLATFORM_INC)

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
COMP_SRC := $(shell find $(ENGINE_DIR) -name '*.comp')
SPIRV := $(patsubst $(ENGINE_DIR)/%.vert,$(OUTPUT_LINUX)/%.vert.spv,$(VERT_SRC)) \
         $(patsubst $(ENGINE_DIR)/%.frag,$(OUTPUT_LINUX)/%.frag.spv,$(FRAG_SRC)) \
         $(patsubst $(ENGINE_DIR)/%.comp,$(OUTPUT_LINUX)/%.comp.spv,$(COMP_SRC))

# -----------------------------------------------------------------------------
# Game sources
# -----------------------------------------------------------------------------

GAME_V_SRC := $(shell find $(GAME_DIR) -name '*.v')
SDK_V_SRC := $(shell find egn -name '*.v')
GAME_MANIFEST := $(GAME_DIR)/v.mod
SDK_MANIFEST := egn/v.mod

GAME_ASSETS_SRC := $(shell find $(GAME_DIR)/assets -type f 2>/dev/null)
GAME_ASSETS := $(patsubst $(GAME_DIR)/assets/%,$(OUTPUT_LINUX)/assets/%,$(GAME_ASSETS_SRC))

# Web build: the C core (scene, components, physics and math) is compiled to
# wasm. The browser-native JS SDK/WebGPU renderer is copied next to it; no
# Node/npm/bundler is required. Physics uses the same Jolt sources rebuilt for
# wasm; math is the same single implementation used natively.
WEB_C_SRC := \
	$(ENGINE_DIR)/platform/web/platform_web.c \
	$(ENGINE_DIR)/platform/web/web_bridge.c \
	$(ENGINE_DIR)/platform/web/web_math.c \
	$(ENGINE_DIR)/core/memo.c \
	$(ENGINE_DIR)/core/logger.c \
	$(ENGINE_DIR)/core/egnstring.c \
	$(ENGINE_DIR)/containers/egnarray.c \
	$(ENGINE_DIR)/math/math.c \
	$(ENGINE_DIR)/scene/scene.c \
	$(ENGINE_DIR)/scene/component.c \
	$(ENGINE_DIR)/physics/physics.c
WEB_OBJ := $(patsubst $(ENGINE_DIR)/%.c,$(WEB_BUILD_DIR)/%.o,$(WEB_C_SRC))
WEB_DEP := $(WEB_OBJ:.o=.d)
WEB_CFLAGS := -O2 -MD -DEGNEXPORT -I$(ENGINE_DIR) -I$(THIRD_PARTY_DIR) $(JOLT_INC)

# `game/v.mod` is the V project root. Add the repository root so `import egn`
# resolves to <repo>/egn. @vlib/@vmodules keep the standard modules available.
V_PATH := $(abspath $(GAME_DIR)/..)|@vlib|@vmodules

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------

.PHONY: all build engine game run clean v jolt emsdk jolt-web web run-web clean-web help

all: engine game

build: all

engine: $(ENGINE_LIB) $(SPIRV)

game: $(GAME_BIN)

# -----------------------------------------------------------------------------
# Web / WASM + WebGPU
# -----------------------------------------------------------------------------

$(WEB_BUILD_DIR)/%.o: $(ENGINE_DIR)/%.c | $(JOLT_WEB_STAMP)
	@echo "  WASM-CC $<"
	@$(MKDIR) $(dir $@)
	@$(EMCC) $(WEB_CFLAGS) -c $< -o $@

$(WEB_EXPORTS): $(ENGINE_DIR)/scene/scene.h $(ENGINE_DIR)/platform/web/web_bridge.c $(ENGINE_DIR)/platform/web/web_math.c
	@echo "  WASM-EXPORTS $@"
	@$(MKDIR) $(dir $@)
	@(grep -hoE 'egn_[a-zA-Z0-9_]+' $^ | sort -u | sed 's/^/_/' ; echo _malloc; echo _free) > $@

$(WEB_ENGINE_JS): $(WEB_OBJ) $(JOLT_WEB_LIBS) $(WEB_EXPORTS)
	@echo "  WASM-LD $@"
	@$(MKDIR) $(dir $@)
	@$(EMXX) $(WEB_OBJ) $(JOLT_WEB_LIBS) -o $@ \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createEgnModule \
		-sENVIRONMENT=web -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB \
		-sSTACK_SIZE=2MB -sFILESYSTEM=0 \
		-sEXPORTED_FUNCTIONS=@$(WEB_EXPORTS) \
		-sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPF32,HEAPU32

web: $(WEB_ENGINE_JS)
	@echo "  WEB  copying JavaScript SDK and demo"
	@rm -rf $(WEB_DIST)/web $(WEB_DIST)/engine $(WEB_DIST)/assets
	@$(MKDIR) $(WEB_DIST)/web $(WEB_DIST)/engine/renderer $(WEB_DIST)/assets
	@$(COPY) -r $(WEB_DIR)/sdk $(WEB_DIST)/web/sdk
	@$(COPY) -r $(WEB_DIR)/demo $(WEB_DIST)/web/demo
	@$(COPY) -r $(ENGINE_DIR)/renderer/webgpu $(WEB_DIST)/engine/renderer/webgpu
	@$(COPY) $(WEB_DIR)/demo/index.html $(WEB_DIST)/index.html
	@$(COPY) -r $(GAME_DIR)/assets/. $(WEB_DIST)/assets/
	@echo "  WEB  demo ready in $(WEB_DIST)/"

run-web: web
	@echo "  WEB  opening $(WEB_DIST)/index.html"
	@if [ -n "$(WEB_BROWSER)" ]; then \
		"$(WEB_BROWSER)" $(WEB_BROWSER_FLAGS) --user-data-dir=$(WEB_PROFILE) \
			"file://$(abspath $(WEB_DIST)/index.html)" >/dev/null 2>&1 & \
	elif command -v $(WEB_OPEN) >/dev/null 2>&1; then \
		$(WEB_OPEN) "file://$(abspath $(WEB_DIST)/index.html)" >/dev/null 2>&1 || true; \
	else \
		echo "open this file manually: $(abspath $(WEB_DIST)/index.html)"; \
	fi

clean-web:
	@echo "  CLEAN web"
	@rm -rf $(WEB_DIST) $(WEB_BUILD_DIR)

emsdk:
	@if [ ! -x "$(EMCC)" ]; then \
		git clone --depth 1 https://github.com/emscripten-core/emsdk.git $(EMSDK_DIR); \
		cd $(EMSDK_DIR) && ./emsdk install latest && ./emsdk activate latest; \
	else \
		echo "Emscripten already installed at $(EMSDK_DIR)"; \
	fi

$(JOLT_WEB_LIBS): $(JOLT_WEB_STAMP)
	@if [ ! -f "$@" ]; then $(MAKE) jolt-web; fi

$(JOLT_WEB_STAMP):
	@if [ ! -x "$(EMCMAKE)" ]; then \
		echo "error: Emscripten not found. Run 'make emsdk' first."; \
		exit 1; \
	fi
	@if [ ! -d $(JOLTC_DIR) ] || [ ! -d $(JOLT_DIR) ]; then $(MAKE) jolt; fi
	@echo "  JOLT-WEB configuring"
	@rm -rf $(JOLTC_WEB_DIR)
	@$(EMCMAKE) $(CMAKE) -S $(JOLTC_DIR) -B $(JOLTC_WEB_DIR) \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_CXX_FLAGS="-include type_traits" \
		-DJPH_SAMPLES=OFF -DJPH_BUILD_SHARED=OFF \
		-DENABLE_ALL_WARNINGS=OFF \
		-DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF \
		-DPROFILER_IN_DEBUG_AND_RELEASE=OFF \
		-DDEBUG_RENDERER_IN_DISTRIBUTION=OFF
	@echo "  JOLT-WEB building"
	@$(CMAKE) --build $(JOLTC_WEB_DIR) -j$$(nproc)
	@touch $(JOLT_WEB_STAMP)

$(ENGINE_LIB): $(ENGINE_OBJ) $(JOLT_LIBS)
	@echo "  LD   $@"
	@$(MKDIR) $(dir $@)
	@$(CXX) $(PLATFORM_LDFLAGS) $(ENGINE_OBJ) $(JOLT_LIBS) $(PLATFORM_LIBS) -o $@

$(BUILD_DIR)/$(ENGINE_DIR)/%.o: $(ENGINE_DIR)/%.c | $(JOLT_STAMP)
	@echo "  CC   $<"
	@$(MKDIR) $(dir $@)
	@$(CC) $(C_FLAGS) $(PLATFORM_CFLAGS) $(DEFINES) $(INC_FLAGS) -c $< -o $@

$(OUTPUT_LINUX)/%.spv: $(ENGINE_DIR)/%
	@echo "  SPV  $<"
	@$(MKDIR) $(dir $@)
	@$(GLSLANG) $< -o $@

$(OUTPUT_LINUX)/%.comp.spv: $(ENGINE_DIR)/%.comp
	@echo "  SPV  $<"
	@$(MKDIR) $(dir $@)
	@$(GLSLANG) $< -o $@

$(JOLT_LIBS): $(JOLT_STAMP)
	@if [ ! -f "$@" ]; then $(MAKE) jolt; fi

$(JOLT_STAMP):
	@$(MAKE) jolt

$(GAME_BIN): $(GAME_V_SRC) $(SDK_V_SRC) $(GAME_MANIFEST) $(SDK_MANIFEST) $(GAME_ASSETS)
	@if [ ! -f $(ENGINE_LIB) ]; then \
		echo "error: $(ENGINE_LIB) not found; run 'make engine' first"; \
		exit 1; \
	fi
	@echo "  V    $@"
	@$(MKDIR) $(dir $@)
	@$(V) $(V_FLAGS) -path '$(V_PATH)' -o $@ $(GAME_DIR)

$(OUTPUT_LINUX)/assets/%: $(GAME_DIR)/assets/%
	@echo "  COPY $<"
	@$(MKDIR) $(dir $@)
	@$(COPY) $< $@

run: all
	@$(RUN_CMD)

clean:
	@echo "  CLEAN build/ output/"
	@$(CLEAN_CMD)

v:
	@if [ ! -x third_part/v/v ]; then \
		git clone --depth 1 https://github.com/vlang/v third_part/v; \
		$(MAKE) -C third_part/v; \
	else \
		echo "V already built at third_part/v/v"; \
	fi

jolt:
	@if [ ! -d $(JOLTC_DIR) ]; then \
		git clone --depth 1 https://github.com/jrouwe/JoltC $(JOLTC_DIR); \
	fi
	@if [ ! -d $(JOLT_DIR) ]; then \
		git clone --depth 1 --branch v5.3.0 https://github.com/jrouwe/JoltPhysics $(JOLT_DIR); \
	fi
	@if [ -d $(JOLT_DIR)/.git ]; then \
		git -C $(JOLT_DIR) fetch --depth 1 origin tag v5.3.0 >/dev/null 2>&1 || true; \
		git -C $(JOLT_DIR) checkout v5.3.0 >/dev/null 2>&1 || true; \
	fi
	@if [ ! -x "$(CMAKE)" ] && ! command -v "$(CMAKE)" >/dev/null 2>&1; then \
		echo "error: CMake not found. Install cmake or run: make CMAKE=/path/to/cmake jolt"; \
		exit 1; \
	fi
	@echo "  JOLT configuring"
	@rm -rf $(JOLTC_DIR)/build
	@$(CMAKE) -S $(JOLTC_DIR) -B $(JOLTC_DIR)/build \
		-DCMAKE_BUILD_TYPE=Release \
		-DJPH_SAMPLES=OFF -DJPH_BUILD_SHARED=OFF \
		-DJPH_USE_VK=OFF -DJPH_USE_DX12=OFF -DJPH_USE_MTL=OFF -DJPH_USE_CPU_COMPUTE=ON \
		-DENABLE_ALL_WARNINGS=OFF \
		-DDEBUG_RENDERER_IN_DEBUG_AND_RELEASE=OFF \
		-DPROFILER_IN_DEBUG_AND_RELEASE=OFF \
		-DDEBUG_RENDERER_IN_DISTRIBUTION=OFF
	@echo "  JOLT building"
	@$(CMAKE) --build $(JOLTC_DIR)/build -j$$(nproc)
	@touch $(JOLT_STAMP)

help:
	@echo "EGN build targets:"
	@echo "  make / make all   build engine + game"
	@echo "  make engine       build only the engine (library + shaders)"
	@echo "  make game         build only the game (requires the engine)"
	@echo "  make build        alias for 'make all'"
	@echo "  make run          build everything and run the game"
	@echo "  make clean        remove build/ and output/"
	@echo "  make v            clone/build V in third_part/v"
	@echo "  make jolt         clone/build JoltC + Jolt in third_part/"
	@echo "  make emsdk        clone/install Emscripten in third_part/"
	@echo "  make jolt-web     build JoltC/Jolt for WebAssembly"
	@echo "  make web          build wasm core + JS SDK + WebGPU demo"
	@echo "  make run-web      build and open output/web/index.html"
	@echo "  make clean-web    remove web build artifacts"
	@echo "  make help         show this help"
	@echo ""
	@echo "Variables:"
	@echo "  PLATFORM=$(PLATFORM)  CC=$(CC)  CXX=$(CXX)  V=$(V)"
	@echo "  EMCC=$(EMCC)  WEB_BROWSER=$(WEB_BROWSER)"
	@echo "  Override: make PLATFORM=linux CC=clang V=/path/to/v"

-include $(ENGINE_DEP)
-include $(WEB_DEP)
