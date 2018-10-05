CC=g++-8
CFLAGS=-O0 -g -std=gnu++17
LIBS=-lnuma -fopenmp -pthread

EXEC=main

all: main

$(EXEC): main.cpp
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

run_local: $(EXEC)
	numactl --physcpubind=0-6 ./$(EXEC)

run_single: $(EXEC)
	numactl --physcpubind=0 ./$(EXEC)

run_all: $(EXEC)
	./$(EXEC)

clean:
	rm -f main
