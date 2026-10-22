# Wirelet build rules.
#
# make          build the C and C++ static libraries
# make test     run the cross-language test suite
# make clean    remove generated build files

CC ?= gcc
CXX ?= g++
AR ?= ar
PYTHON ?= python3

BUILD_DIR ?= build
CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -Wpedantic
CPPFLAGS ?=
DEPFLAGS := -MMD -MP

C_LIB := $(BUILD_DIR)/libwirelet.a
CPP_LIB := $(BUILD_DIR)/libwireletcpp.a

C_OBJECTS := $(BUILD_DIR)/c/wirelet.o $(BUILD_DIR)/c/crc32.o
CPP_OBJECTS := $(BUILD_DIR)/cpp/wirelet.o $(BUILD_DIR)/cpp/crc32.o

.PHONY: all libs c cpp test check clean help

all: libs

libs: $(C_LIB) $(CPP_LIB)

c: $(C_LIB)

cpp: $(CPP_LIB)

$(C_LIB): $(C_OBJECTS)
	$(AR) rcs $@ $^

$(CPP_LIB): $(CPP_OBJECTS)
	$(AR) rcs $@ $^

$(BUILD_DIR)/c/%.o: c/%.c | $(BUILD_DIR)/c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -I c -c $< -o $@

$(BUILD_DIR)/cpp/%.o: cpp/%.c | $(BUILD_DIR)/cpp
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPFLAGS) -I cpp -c $< -o $@

$(BUILD_DIR)/cpp/%.o: cpp/%.cpp | $(BUILD_DIR)/cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(DEPFLAGS) -I cpp -c $< -o $@

$(BUILD_DIR)/c $(BUILD_DIR)/cpp:
	mkdir -p $@

test: all
	CC="$(CC)" CXX="$(CXX)" $(PYTHON) tests/run_tests.py

check: test

clean:
	rm -rf $(BUILD_DIR) tests/build py/__pycache__ tests/__pycache__

help:
	@printf '%s\n' \
		'Targets:' \
		'  all     build the C and C++ static libraries (default)' \
		'  libs    same as all' \
		'  c       build only build/libwirelet.a' \
		'  cpp     build only build/libwireletcpp.a' \
		'  test    run the cross-language test suite' \
		'  check   same as test' \
		'  clean   remove generated build files' \
		'' \
		'Variables: CC, CXX, AR, PYTHON, BUILD_DIR, CFLAGS, CXXFLAGS, CPPFLAGS'

-include $(C_OBJECTS:.o=.d) $(CPP_OBJECTS:.o=.d)
