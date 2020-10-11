
CFLAGS := -O0 -g -Wall -Werror -Iinc -std=c11 -Ideps/logc/src
LDFLAGS := 

all: main wfc_test
	./wfc_test

main: wfc.o log.o src/main.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

wfc_test: inc/wfc.h log.o src/wfc.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -DWFC_TEST -DWFC_TEST_MAIN

wfc.o: inc/wfc.h src/wfc.c
	$(CC) -c $^ $(CFLAGS) $(LDFLAGS)

log.o: deps/logc/src/log.c
	$(CC) -c $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean
clean:
	-@rm main
	-@rm wfc_test
	-@rm wfc.o
