all:
	cc -g -Wall -pedantic -o lightgunxr lightgunxr.c -lm -lopenxr_loader

clean:
	rm -f lightgunxr
