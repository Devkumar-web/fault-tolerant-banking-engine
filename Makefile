CC ?= gcc
MODE ?= debug
CPPFLAGS += -Iinclude -D_XOPEN_SOURCE=700 -D_DEFAULT_SOURCE
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wformat=2 -Werror -MMD -MP
ifeq ($(MODE),release)
CFLAGS += -O2
else ifeq ($(MODE),sanitize)
CFLAGS += -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined -fno-pie
LDFLAGS += -fsanitize=address,undefined -no-pie
else ifeq ($(MODE),debug)
CFLAGS += -O0 -g3
else
$(error MODE must be debug, release, or sanitize)
endif
BUILD = build/$(MODE)
LIBSRC = $(filter-out src/server.c src/client.c src/database.c src/legacy_server.c src/legacy_client.c,$(wildcard src/*.c))
LIBOBJ = $(patsubst src/%.c,$(BUILD)/%.o,$(LIBSRC))
LDLIBS += -lcrypt -lm
.PHONY: all check test clean debug release sanitize
all: $(addprefix $(BUILD)/,server client database)
$(BUILD)/%.o: src/%.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD)/server: $(BUILD)/server.o $(LIBOBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/database: $(BUILD)/database.o $(LIBOBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/client: $(BUILD)/client.o $(BUILD)/protocol.o $(BUILD)/financial_utils.o
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/test_core: tests/test_core.c $(LIBOBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c %.o,$^) $(LDLIBS) -o $@
$(BUILD)/legacy_server: $(BUILD)/legacy_server.o $(LIBOBJ)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@
$(BUILD)/requests_test.o: src/requests.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBANK_TESTING -c $< -o $@
$(BUILD)/account_service_test.o: src/account_service.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBANK_TESTING -c $< -o $@
$(BUILD)/server_runtime_test.o: src/server_runtime.c
	@mkdir -p $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBANK_TESTING -c $< -o $@
$(BUILD)/test_phase2: tests/test_phase2.c $(filter-out $(BUILD)/requests.o $(BUILD)/account_service.o $(BUILD)/server_runtime.o,$(LIBOBJ)) $(BUILD)/requests_test.o $(BUILD)/account_service_test.o $(BUILD)/server_runtime_test.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(filter %.c %.o,$^) $(LDLIBS) -o $@
check test: all $(BUILD)/test_core $(BUILD)/legacy_server $(BUILD)/test_phase2
	python3 tests/run_tests.py $(BUILD)
	python3 tests/run_phase2.py $(BUILD)
debug release sanitize:
	$(MAKE) MODE=$@ check
clean:
	rm -rf build
-include $(wildcard $(BUILD)/*.d)
