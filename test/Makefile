# Builds Moneta's EMV parser and exposure grader for the host, with the
# address and undefined-behaviour sanitisers on, and runs every check.
#
# The parser eats attacker-controlled bytes off a radio, and the grade is the
# only part of the app a user actually acts on. Neither is something to verify
# by looking at a screenshot, so both are plain C with no Flipper headers —
# precisely so this Makefile can exist.
#
#   make -C test

CC     ?= cc
CFLAGS := -std=gnu11 -Wall -Wextra -Werror -O1 -g
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
CFLAGS += -I ..

SRC := host_emv_test.c \
       ../helpers/emv_tlv.c \
       ../helpers/emv_card.c \
       ../helpers/leak_grade.c \
       ../helpers/demo_card.c \
       ../helpers/report.c

DEPS := ../helpers/emv_tlv.h \
        ../helpers/emv_card.h \
        ../helpers/leak_grade.h \
        ../helpers/demo_card.h \
        ../helpers/demo_card_data.h \
        ../helpers/report.h

BIN := host_emv_test

.PHONY: all run clean
all: run

$(BIN): $(SRC) $(DEPS)
	$(CC) $(CFLAGS) $(SRC) -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM
