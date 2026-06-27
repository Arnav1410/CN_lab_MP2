compile: smserver.c smclient.c
	gcc -Wall smserver.c -o smserver
	gcc -Wall smclient.c -o smclient

clean:
	-rm -rf smserver smclient
