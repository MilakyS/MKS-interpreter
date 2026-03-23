CC = gcc

CFLAGS = -std=c2x -Wall -Wextra -pedantic -O3
DEBUG_FLAGS = -std=c2x -Wall -Wextra -pedantic -g -O0
SAN_FLAGS = -std=c2x -Wall -Wextra -pedantic -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer

LDFLAGS = -lm
SAN_LDFLAGS = -fsanitize=address,undefined -lm

TARGET = mks_run
SAN_TARGET = mks_run_san

SRC = \
	main.c \
	Lexer/lexer.c \
	Parser/AST.c \
	Parser/parser_core.c \
	Parser/parser_expr.c \
	Parser/parser_stmt.c \
	Eval/eval.c \
	Runtime/value.c \
	env/env.c \
	Runtime/output.c \
	Runtime/operators.c \
	Runtime/functions.c \
	Runtime/indexing.c \
	Runtime/methods.c \
	Runtime/control_flow.c \
	Utils/hash.c \
	GC/gc.c

OBJ = $(SRC:.c=.o)
SAN_OBJ = $(SRC:.c=.san.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

san: clean $(SAN_TARGET)

$(SAN_TARGET): $(SAN_OBJ)
	$(CC) $(SAN_OBJ) -o $(SAN_TARGET) $(SAN_LDFLAGS)

%.san.o: %.c
	$(CC) $(SAN_FLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(SAN_OBJ) $(TARGET) $(SAN_TARGET)

run:
	./$(TARGET)

run-san:
	ASAN_OPTIONS=detect_leaks=1 ./$(SAN_TARGET)

.PHONY: all clean san run run-san
