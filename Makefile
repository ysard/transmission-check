
main:
	gcc -O2 -Wall -Wextra -L./lib -L./include/dht -L./include/libnatpmp -L./include/miniupnp -L./include/libutp -I./include src/main.c -o main -ltransmission -lz -levent -lpthread -lssl -lcrypto -lcurl -lnatpmp -lminiupnpc -lutp -ldht # -pedantic

rights: main
	chmod +x main
del:
	-rm main
run: del rights
	./main

r: run