# Variáveis
CC = clang
EXE = game_test

SRC_DIR = game_test
BUILD_DIR = bin
OBJ_DIR = obj

SRC = $(shell find $(SRC_DIR) -name '*.c')
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

ENGINE_LIB = engine
ENGINE_INC = engine

C_FLAGS = -g
INC_FLAGS = -I$(SRC_DIR) -I$(ENGINE_INC) -I/usr/include
LINK_FLAGS = -Lbin -l$(ENGINE_LIB) -Wl,-rpath,'$$ORIGIN'
DEFINES = 

all: build

build: $(BUILD_DIR)/$(EXE)

$(BUILD_DIR)/$(EXE): $(OBJ) $(BUILD_DIR)/$(ENGINE_LIB)
	mkdir -p $(BUILD_DIR)
	$(CC) $(OBJ) $(LINK_FLAGS) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(C_FLAGS) $(DEFINES) $(INC_FLAGS) -c $< -o $@

$(BUILD_DIR)/$(ENGINE_LIB):
	make -f Makefile.engine.linux.mak

run: build
	./$(BUILD_DIR)/$(EXE)

clean:
	rm -rf $(OBJ_DIR) $(BUILD_DIR)

.PHONY: all build run clean
