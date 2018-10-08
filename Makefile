CC=gcc-8
CFLAGS=-O0 -g
LIBS=-lnuma -fopenmp -pthread

EXEC=main
BENCHMARK=bandwidth_bench_with_perf
INTERLEAVE_BUG=force_interleave

all: $(BENCHMARK) $(INTERLEAVE_BUG)

#$(EXEC): main.cpp
#	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BENCHMARK): bandwidth_bench_with_perf.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(INTERLEAVE_BUG): force_interleave.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

run_local: $(EXEC)
	numactl --physcpubind=0-6 ./$(EXEC)

run_single: $(EXEC)
	numactl --physcpubind=0 ./$(EXEC)

run_all: $(EXEC)
	./$(EXEC)

clean:
	rm -f *.o $(EXEC) $(BENCHMARK)
