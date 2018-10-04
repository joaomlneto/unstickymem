CC=g++
CFLAGS=-O0 -g
LIBS=-lnuma

# list of targets
MAIN=main
EXECS=$(MAIN)

all: $(EXECS)

$(MAIN): main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(MAIN)
