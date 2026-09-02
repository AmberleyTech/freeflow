# FreeFlow Stats sidecar — tiny portable C99 binary.
# macOS is the production target; Linux builds exist so the engine and tests
# can run in CI/containers. Only libsqlite3 is linked.

UNAME_S  := $(shell uname -s)
ifeq ($(origin CC),default)
CC       := clang
endif
CSTD     := -std=c99
WARN     := -Wall -Wextra -Wpedantic
OPT      := -Os -flto
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE
LDLIBS   += -lsqlite3

ifeq ($(UNAME_S),Darwin)
OPT      += -mmacosx-version-min=13.0
LDFLAGS  += -Wl,-dead_strip
endif

SRC      := $(wildcard src/*.c) tests/tests.c
OBJ      := $(SRC:.c=.o)
BIN      := build/freeflow-stats
APP      := build/FreeFlow Stats.app

DATA_DIR  := $(HOME)/Library/Application Support/FreeFlowStats
AGENT_DIR := $(HOME)/Library/LaunchAgents
AGENT_ID  := com.freeflowstats.agent

.PHONY: all app check memcheck diskcheck install uninstall run clean

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CC) $(OPT) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
ifeq ($(UNAME_S),Darwin)
	@strip -x $@ 2>/dev/null || true
else
	@strip $@ 2>/dev/null || true
endif
	@ls -l $@

%.o: %.c $(wildcard src/*.h)
	$(CC) $(CSTD) $(WARN) $(OPT) $(CPPFLAGS) -c -o $@ $<

# Optional viewer: a tiny WKWebView wrapper so stats open as a real app
# (Dock / Spotlight / Cmd-Tab / keyboard shortcuts). On-demand only — the
# always-on collector stays the 0 MB-idle launchd agent.
app: $(BIN)
ifeq ($(UNAME_S),Darwin)
	@mkdir -p "$(APP)/Contents/MacOS"
	$(CC) -Os -fobjc-arc -framework Cocoa -framework WebKit \
	    -o "$(APP)/Contents/MacOS/FreeFlowStats" app/main.m
	cp app/Info.plist "$(APP)/Contents/Info.plist"
	@echo "Built $(APP)"
else
	@echo "The viewer app is macOS-only; the engine and page work everywhere."
endif

check: $(BIN)
	$(BIN) --test

memcheck: $(BIN)
	$(BIN) --memcheck

diskcheck: $(BIN)
	$(BIN) --diskcheck

run: $(BIN)
	$(BIN)

install: $(BIN) app
ifeq ($(UNAME_S),Darwin)
	@mkdir -p "$(DATA_DIR)/bin" "$(AGENT_DIR)" "$(HOME)/Applications"
	cp "$(BIN)" "$(DATA_DIR)/bin/freeflow-stats"
	rm -rf "$(HOME)/Applications/FreeFlow Stats.app"
	cp -R "$(APP)" "$(HOME)/Applications/FreeFlow Stats.app"
	sed -e "s|__BINARY__|$(DATA_DIR)/bin/freeflow-stats|g" \
	    -e "s|__HOME__|$(HOME)|g" \
	    $(AGENT_ID).plist > "$(AGENT_DIR)/$(AGENT_ID).plist"
	-launchctl bootout gui/$(shell id -u)/$(AGENT_ID) 2>/dev/null
	launchctl bootstrap gui/$(shell id -u) "$(AGENT_DIR)/$(AGENT_ID).plist"
	@echo "Installed. The agent now watches FreeFlow's history database."
	@echo "Stats page: $(DATA_DIR)/stats.html"
	@echo "Viewer app: $(HOME)/Applications/FreeFlow Stats.app"
else
	@echo "install is macOS-only (launchd). Use 'make run' on this platform."
endif

uninstall:
ifeq ($(UNAME_S),Darwin)
	-launchctl bootout gui/$(shell id -u)/$(AGENT_ID) 2>/dev/null
	rm -f "$(AGENT_DIR)/$(AGENT_ID).plist"
	rm -rf "$(HOME)/Applications/FreeFlow Stats.app"
	@echo "Agent and viewer app removed. Data left in place at: $(DATA_DIR)"
	@echo "Delete that directory too if you want a full removal."
else
	@echo "uninstall is macOS-only."
endif

clean:
	rm -rf build $(OBJ)
