CC = clang
CFLAGS = -O3 -std=c11 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -D_DEFAULT_SOURCE
BINS = bin/train bin/bench bin/odds bin/test bin/solve_test

all: $(BINS)

bin/%: src/%.c src/poker.h src/solver.h
	@mkdir -p bin data
	$(CC) $(CFLAGS) -o $@ $< -lpthread -lm

test: bin/test bin/solve_test
	./bin/test
	./bin/solve_test

clean:
	rm -rf bin
