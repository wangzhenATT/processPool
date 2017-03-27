.PHONY:all
all:client useprocpool

client:client.c
	g++ -o $@ $^
useprocpool:useprocpool.c
	g++ -g -o $@ $^

.PHONY:clean
clean:
	rm -f useprocpool client
