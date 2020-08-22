all: slope

CXX := g++

CXX_SRCS = $(wildcard *.cc)
CXX_OBJS := $(patsubst %.cc,%.o,$(CXX_SRCS))
CXX_DEPS := $(patsubst %.cc,%.d,$(CXX_SRCS))

CXX_INCLUDE := -I. -I/extra-pkg/boost-1-72/include
CXX_WARNINGS=-pedantic -Wall -Wextra -Wcast-align -Wcast-qual \
	-Wctor-dtor-privacy -Wdisabled-optimization -Wformat=2 -Winit-self \
	-Wmissing-declarations \
	-Wold-style-cast -Woverloaded-virtual -Wredundant-decls -Wshadow \
	-Wsign-conversion -Wsign-promo \
	-Wstrict-overflow=2 -Wswitch-default -Wundef -Werror -Wno-unused \
	-Wall -Wextra -Wformat-nonliteral -Wcast-align -Wpointer-arith \
	-Wmissing-declarations -Wundef -Wcast-qual \
	-Wshadow -Wwrite-strings -Wno-unused-parameter -Wfloat-equal \
	-pedantic


CXX_FLAGS := -std=c++17
CXX_FLAGS += -DSLOPE_DEBUG
CXX_FLAGS += -O2

%.o: %.cc $(CXX_HEADERS) Makefile
	$(CXX) $(CXX_FLAGS) $(CXX_WARNINGS) $(CXX_INCLUDE) -MMD -MP -c -o $@ $<

slope: $(CXX_OBJS)
	$(CXX) $(CXX_FLAGS) $(CXX_INCLUDE) -Wl,-rpath=/extra-pkg/boost-1-72/lib -L/extra-pkg/boost-1-72/lib -lboost_system -pthread -lrdmacm -libverbs -lmemcached -o $@ $^

all: slope

.PHONY: all clean

-include $(CXX_DEPS)

clean:
	rm slope $(CXX_OBJS) $(CXX_DEPS)
