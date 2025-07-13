# Compilador
CC=gcc

# Flags de compilação
# -Wall: ativa todos os warnings
# -g: adiciona informações de debug
CFLAGS=-Wall -g

# Alvos
TARGETS=client server

# Regra principal
all: $(TARGETS)

# Regra para compilar o cliente
client: client.c protocol_defs.h
	$(CC) $(CFLAGS) -o client client.c

# Regra para compilar o servidor
server: server.c protocol_defs.h
	$(CC) $(CFLAGS) -o server server.c

# Regra para limpar os arquivos compilados e executáveis
clean:
	rm -f $(TARGETS) *.o

# Phony targets não representam arquivos
.PHONY: all clean