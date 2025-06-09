SRCS := $(shell find src -type f -name '*.c')
OBJS = $(SRCS:.c=.o)
LIBS = glfw3

include build.config

CFLAGS += -std=c99 -Wall -Wextra -Wpedantic -rdynamic $(shell pkg-config --cflags --libs $(LIBS)) -Isrc/stdlib/graphics/glad_gl330/include

DEF_FLAGS=-DPROJECT_NAME=\"$(PROJECT_NAME)\" -DMAJOR_VERSION=\"$(MAJOR_VERSION)\" -DMINOR_VERSION=\"$(MINOR_VERSION)\" -D_XOPEN_SOURCE=700

export CC
export CFLAGS

release: CFLAGS += -O2 $(DEF_FLAGS)
release: bin/esh

debug: CFLAGS += -g -fsanitize=address,leak,undefined -DDEBUG $(DEF_FLAGS)
debug: bin/esh

bin/esh: $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) -obin/esh $(shell pkg-config --cflags --libs $(LIBS))

.PHONY: docs
docs: bin/esh
	./bin/esh build-docs.esh

clean:
	rm -f $(OBJS)
	rm -f bin/*
	rm -f docs/*.html
	rm -f docs/*.json

test: CFLAGS += -g -fsanitize=address,leak,undefined -DDEBUG -Isrc
test: debug
	./run_tests.sh
