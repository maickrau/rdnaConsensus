GPP=$(CXX)
CPPFLAGS=-Wall -Wextra -std=c++17 -O3 -g -Izstr/src -Iparallel-hashmap/parallel_hashmap/ -Icxxopts/include -Wno-unused-parameter `pkg-config --cflags zlib`

ODIR=obj
BINDIR=bin
SRCDIR=src

LIBS=`pkg-config --libs zlib`

_DEPS = fastqloader.h FastHasher.h VerkkoReadAssignment.h ReadExtractor.h KmerMatcher.h ClusterHandler.h VerkkoTangleGuesser.h RibotinUtils.h WfaHelper.h TwobitString.h
DEPS = $(patsubst %, $(SRCDIR)/%, $(_DEPS))

_OBJ = fastqloader.o FastHasher.o VerkkoReadAssignment.o ReadExtractor.o KmerMatcher.o ClusterHandler.o VerkkoTangleGuesser.o RibotinUtils.o WfaHelper.o TwobitString.o
OBJ = $(patsubst %, $(ODIR)/%, $(_OBJ))

LINKFLAGS = $(CPPFLAGS) -Wl,-Bstatic $(LIBS) -Wl,-Bdynamic -Wl,--as-needed -lpthread -pthread -static-libstdc++

TEMPLATEPATH := $(realpath template_seqs)
VERSION := Branch $(shell git rev-parse --abbrev-ref HEAD) commit $(shell git rev-parse HEAD) $(shell git show -s --format=%ci)

$(shell mkdir -p bin)
$(shell mkdir -p obj)

all: $(BINDIR)/ribotin-verkko $(BINDIR)/ribotin-ref

$(BINDIR)/ribotin-verkko: $(OBJ) $(ODIR)/ribotin-verkko.o
	$(GPP) -o $@ $^ $(LINKFLAGS)

$(BINDIR)/ribotin-ref: $(OBJ) $(ODIR)/ribotin-ref.o
	$(GPP) -o $@ $^ $(LINKFLAGS)

$(ODIR)/ribotin-verkko.o: $(SRCDIR)/ribotin-verkko.cpp $(DEPS) $(OBJ)
	$(GPP) -c -o $@ $< $(CPPFLAGS) -DVERSION="\"$(VERSION)\"" -DRIBOTIN_TEMPLATE_PATH="\"$(TEMPLATEPATH)\""

$(ODIR)/ribotin-ref.o: $(SRCDIR)/ribotin-ref.cpp $(DEPS) $(OBJ)
	$(GPP) -c -o $@ $< $(CPPFLAGS) -DVERSION="\"$(VERSION)\"" -DRIBOTIN_TEMPLATE_PATH="\"$(TEMPLATEPATH)\""

$(ODIR)/%.o: $(SRCDIR)/%.cpp $(DEPS)
	$(GPP) -c -o $@ $< $(CPPFLAGS)

clean:
	rm -f $(ODIR)/*
	rm -f $(BINDIR)/*
