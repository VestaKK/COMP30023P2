CC 		  := gcc
CCFLAGS   := -Wall

BUILD	  := build
OBJ_DIR	  := $(BUILD)/obj
SRC_DIR	  := src

INCFLAGS  := -Iinclude
LDFLAGS	  := -lm -lpthread
SRC 	  := $(wildcard $(SRC_DIR)/*.c)
OBJ 	  := $(SRC:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

RPC_SYS = rpc.a
SERVER = server
CLIENT = client

default: dirs $(RPC_SYS)

all: dirs $(RPC_SYS) $(SERVER) $(CLIENT)

echo:
	-@echo $(SRC)
	-@echo $(OBJ)

$(RPC_SYS): $(OBJ)
	ar rcs $@ $^

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CCFLAGS) -c $^ -o $@ $(INCFLAGS) $(LDFLAGS) 

$(SERVER): server.a $(RPC_SYS) 
	$(CC) $(CCFLAGS) -o $@ $^ $(INCFLAGS) $(LDFLAGS)

$(CLIENT): client.a $(RPC_SYS) 
	$(CC) $(CCFLAGS) -o $@ $^ $(INCFLAGS) $(LDFLAGS)

dirs:
	@mkdir -p $(BUILD)
	@mkdir -p $(OBJ_DIR)

clean:
	-@rm -rf $(BUILD)
	-@rm -f $(RPC_SYS)
	-@rm -f $(SERVER)
	-@rm -f $(CLIENT)