# Variáveis
CC = clang
LIB = libengine.so

SRC_DIR = engine
BUILD_DIR = bin
OBJ_DIR = obj

SRC = $(shell find $(SRC_DIR) -name '*.c')
OBJ = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC))

C_FLAGS = -g -Wvarargs -Wall -Werror -fPIC
INC_FLAGS = -I$(SRC_DIR) -I/usr/include
LINK_FLAGS = -shared -lwayland-client -lvulkan
DEFINES = -DEGNEXPORT

# Regras para compilar a biblioteca compartilhada
all: $(BUILD_DIR)/libengine.so

# Cria a biblioteca compartilhada
$(BUILD_DIR)/libengine.so: $(OBJ)
	mkdir -p $(BUILD_DIR)
	$(CC) $(C_FLAGS) $(LINK_FLAGS) $(OBJ) -o $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(C_FLAGS) $(DEFINES) $(INC_FLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(BUILD_DIR)

.PHONY: all clean
