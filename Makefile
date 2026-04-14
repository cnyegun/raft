CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -g
SANITIZERS ?=

TEST_BIN := test_raft.out
TEST_SRC := raft.c test_raft.c

.PHONY: test clean

$(TEST_BIN): $(TEST_SRC)
	$(CC) $(CFLAGS) $(SANITIZERS) $(TEST_SRC) -o $(TEST_BIN)

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -f $(TEST_BIN)
