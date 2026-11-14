
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra

SOURCES_PESTO11 = main.cpp model.cpp parser.cpp tokenizer.cpp utility.cpp validator.cpp validator-expr.cpp validator-std.cpp \
	codeline.cpp generator.cpp generator-stmt.cpp generator-expr.cpp generator-var.cpp generator-str.cpp generator-set.cpp generator-real.cpp generator-file.cpp generator-std.cpp asm.cpp runtime.cpp peephole.cpp branches.cpp
SOURCES_TESTRUNNER = testrunner/testrunner.cpp

OBJECTS_PESTO11 = $(SOURCES_PESTO11:.cpp=.o)
OBJECTS_TESTRUNNER = $(SOURCES_TESTRUNNER:.cpp=.o)

all: pesto11 testrunner/testrunner

pesto11: $(OBJECTS_PESTO11)
	$(CXX) $(CXXFLAGS) -o pesto11 $(OBJECTS_PESTO11)

testrunner/testrunner: $(OBJECTS_TESTRUNNER)
	$(CXX) $(CXXFLAGS) -o testrunner/testrunner $(OBJECTS_TESTRUNNER)

$(OBJECTS_PESTO11): main.h

.PHONY: all clean test

test: all
	testrunner/testrunner

clean:
	rm -f $(OBJECTS_PESTO11) $(OBJECTS_TESTRUNNER) pesto11 testrunner/testrunner
	rm -rf tests.temp
