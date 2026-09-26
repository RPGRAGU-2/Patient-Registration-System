CC ?= gcc

CPPFLAGS := -Iinclude
CFLAGS := -std=c17 -Wall -Wextra -Wpedantic -Werror -g -O0
LDFLAGS :=

GTK_CFLAGS := $(shell pkg-config --cflags gtk4 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs gtk4 2>/dev/null)
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS := $(shell pkg-config --libs sqlite3 2>/dev/null)
CRYPT_LIBS := -lcrypt

BUILD_DIR := build
APP_BIN := $(BUILD_DIR)/prs
APP_SOURCES := \
	src/main.c \
	src/app_config.c \
	src/logging.c \
	src/auth.c \
	src/validation.c \
	src/database.c \
	src/migrations.c \
	src/patient_service.c \
	src/clinical_service.c \
	src/backup_service.c \
	src/ui.c
APP_OBJECTS := $(APP_SOURCES:src/%.c=$(BUILD_DIR)/%.o)
CORE_TEST_BIN := $(BUILD_DIR)/test_validation
INTEGRATION_TEST_BIN := $(BUILD_DIR)/test_patient_service

.PHONY: all app test test-core test-integration check-deps clean help

all: app

help:
	@printf '%s\n' \
	  'Targets:' \
	  '  make test-core  Run dependency-free validation tests' \
	  '  make app        Build the GTK/SQLite desktop application' \
	  '  make test-integration  Run SQLite patient-service tests' \
	  '  make test       Run all tests' \
	  '  make clean      Remove build artefacts'

check-deps:
	@for module in gtk4 sqlite3; do \
		if ! pkg-config --exists $$module; then \
			printf 'Missing development dependency: %s\n' $$module; \
			missing=1; \
		fi; \
	done; \
	if [ "$${missing:-0}" -ne 0 ]; then \
		printf '%s\n' 'Install GTK 4 and SQLite 3 development packages, then run make app.'; \
		exit 1; \
	fi

app: check-deps $(APP_BIN)

$(APP_BIN): $(APP_OBJECTS)
	$(CC) $(LDFLAGS) $^ $(GTK_LIBS) $(SQLITE_LIBS) $(CRYPT_LIBS) -o $@

$(BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(GTK_CFLAGS) $(SQLITE_CFLAGS) -c $< -o $@

$(CORE_TEST_BIN): tests/test_validation.c src/validation.c include/prs/models.h include/prs/validation.h
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_validation.c src/validation.c -o $@

test-core: $(CORE_TEST_BIN)
	./$(CORE_TEST_BIN)

$(INTEGRATION_TEST_BIN): tests/test_patient_service.c src/app_config.c src/logging.c src/validation.c src/database.c src/migrations.c src/auth.c src/patient_service.c src/clinical_service.c src/backup_service.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SQLITE_CFLAGS) $^ $(SQLITE_LIBS) $(CRYPT_LIBS) -o $@

test-integration: $(INTEGRATION_TEST_BIN)
	./$(INTEGRATION_TEST_BIN)

test: test-core test-integration

clean:
	rm -rf $(BUILD_DIR)
