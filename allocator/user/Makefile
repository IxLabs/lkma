CC       =  gcc
LD       =  $(CC)
CFLAGS   =  -Wall -g -O3
SOURCES  =  allocator_test.c
OBJECTS  =  ${SOURCES:.c=.o}
EXE      =  allocator_test

.PHONY: clean

all: build

build: $(EXE)

# Make executable from object files
$(EXE): $(OBJECTS)
	$(LD) -o $@ $^

# Build and compile
run: build
	./$(EXE)

# Remove all generated files
clean:
	rm -rf $(OBJECTS) $(EXE) *~
