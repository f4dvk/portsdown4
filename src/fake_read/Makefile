ifndef CC
CC = gcc
endif

fake_read: fake_read.c
	@echo "  CC     "$@
	@${CC} fake_read.c -o $@

clean:
	@rm -rf fake_read

tags:
	@ctags *

.PHONY: all clean
