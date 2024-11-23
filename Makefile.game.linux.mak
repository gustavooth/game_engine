MAKEFLAGS += -s

NAME = game

CC = clang
EXE = $(NAME)

SRC_DIR = $(NAME)
ASSEMBLY_DIR = bin
BUILD_DIR = build/$(NAME)
CORE_BUILD_DIR = build

SRC = $(shell find $(SRC_DIR) -name '*.c')
OBJ = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC))

ENGINE_LIB = engine
ENGINE_INC_DIR = engine

C_FLAGS = -g -MD
INC_FLAGS = -I$(SRC_DIR) -I$(ENGINE_INC_DIR) -I/usr/include
LINK_FLAGS = -Lbin -l$(ENGINE_LIB) -Wl,-rpath,'$$ORIGIN'
DEFINES = 

all: build

build: $(ASSEMBLY_DIR)/$(EXE)

$(ASSEMBLY_DIR)/$(EXE): $(ASSEMBLY_DIR)/$(ENGINE_LIB) $(OBJ)
	$(info $@)
	mkdir -p $(ASSEMBLY_DIR)
	$(CC) $(OBJ) $(LINK_FLAGS) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(info $@)
	mkdir -p $(dir $@)
	$(CC) $(C_FLAGS) $(DEFINES) $(INC_FLAGS) -c $< -o $@

$(ASSEMBLY_DIR)/$(ENGINE_LIB):
	$(info $@)
	make -f Makefile.engine.linux.mak

run: build
	./$(BUILD_DIR)/$(EXE)

clean:
	$(info $(CORE_BUILD_DIR) $(ASSEMBLY_DIR))
	rm -rf $(CORE_BUILD_DIR) $(ASSEMBLY_DIR)

.PHONY: all build run clean

-include $(OBJ:.o=.d)
