
main:
	gcc -std=gnu11 -O2 -Wall -Wextra -L./lib -L./include/dht -L./include/libnatpmp -L./include/miniupnp -L./include/libutp -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl -lnatpmp -lminiupnpc -lutp -ldht # -pedantic

maind:
	gcc -std=gnu11 -O0 -g -Wall -Wextra -L./lib -L./include/dht -L./include/libnatpmp -L./include/miniupnp -L./include/libutp -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl -lnatpmp -lminiupnpc -lutp -ldht # -pedantic

rights:
	chmod +x main

del:
	-rm main

run: del main rights
	./main

run-debug: del maind rights
	./main

r: run

rd: run-debug