
main:
	gcc -std=gnu11 -O2 -Wall -Wextra -L./lib -L./include/libtransmission -L./include/dht -L./include/libnatpmp -L./include/miniupnp -L./include/libutp -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl -lnatpmp -lminiupnpc -lutp -ldht -o transmission-check # -pedantic

maind:
	gcc -std=gnu11 -O0 -g -Wall -Wextra -L./lib -L./include/libtransmission -L./include/dht -L./include/libnatpmp -L./include/miniupnp -L./include/libutp -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl -lnatpmp -lminiupnpc -lutp -ldht -o transmission-check # -pedantic

val: maind rights
	valgrind --tool=memcheck --leak-check=full --leak-resolution=med --show-reachable=yes -v ./transmission-check

rights:
	chmod +x transmission-check

del:
	-rm transmission-check

all: del main rights
	./transmission-check

all-debug: del maind rights
	./transmission-check

a: all

ad: all-debug