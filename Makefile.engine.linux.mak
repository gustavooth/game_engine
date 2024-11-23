MAKEFLAGS += -s

NAME = engine

CC = clang
LIB = lib$(NAME).so

SRC_DIR = $(NAME)
ASSEMBLY_DIR = bin
BUILD_DIR = build/$(NAME)
CORE_BUILD_DIR = build

SRC = $(shell find $(SRC_DIR) -name '*.c')
OBJ = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(SRC))

C_FLAGS = -g -fPIC -MD -Wvarargs -Wall -Werror -Wno-missing-braces -Werror=vla
INC_FLAGS = -I$(SRC_DIR) -I/usr/include
LINK_FLAGS = -shared -lwayland-client -lvulkan
DEFINES = -DEGNEXPORT

all: $(ASSEMBLY_DIR)/$(LIB)

$(ASSEMBLY_DIR)/$(LIB): $(OBJ)
	$(info $@)
	mkdir -p $(ASSEMBLY_DIR)
	$(CC) $(C_FLAGS) $(LINK_FLAGS) $(OBJ) -o $@

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(info $@)
	mkdir -p $(dir $@)
	$(CC) $(C_FLAGS) $(DEFINES) $(INC_FLAGS) -c $< -o $@

clean:
	$(info $(CORE_BUILD_DIR) $(ASSEMBLY_DIR))
	rm -rf $(build) $(ASSEMBLY_DIR)

.PHONY: all clean

-include $(OBJ:.o=.d)
