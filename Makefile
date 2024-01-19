all:
	cc -g -Wall -pedantic -o lightgunxr lightgunxr.c -lopenxr_loader

clean:
	rm -f lightgunxr
