
CFLAGS := -O0 -g -Wall -Werror -Iinc
LDFLAGS := 

all: main wfc_test
	./wfc_test

main: wfc.o src/main.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

wfc_test: inc/wfc.h src/wfc.c
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS) -DWFC_TEST -DWFC_TEST_MAIN

wfc.o: inc/wfc.h src/wfc.c
	$(CC) -c $^ $(CFLAGS) $(LDFLAGS)

.PHONY: clean
clean:
	-@rm main
	-@rm wfc_test
	-@rm wfc.o
