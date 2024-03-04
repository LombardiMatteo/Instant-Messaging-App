# make rule primaria con dummy target ‘all’--> non crea alcun file all ma fa un complete build
# che dipende dai target client e server scritti sotto
all: dev serv

# make rule per il dev
dev: dev.o
	gcc -Wall dev.o -o dev

# make rule per il server
serv: serv.o
	gcc -Wall serv.o -o serv -lpthread

# pulizia dei file della compilazione (eseguito con ‘make clean’ da terminale)
clean:
	rm *o dev serv