# Transferência Confiável de Arquivos via UDP

Este projeto implementa um protocolo confiável de transferência de arquivos usando o protocolo UDP, simulando perda de pacotes e retransmissões.

## Estrutura do Projeto

├── client/
│   ├── client.c
│   ├── protocol_defs.h
│   ├── Makefile
├── server/
│   ├── server.c
│   ├── protocol_defs.h
│   ├── Makefile


**Nota:** O arquivo `protocol_defs.h` deve ser o mesmo nos dois diretórios, pois define o protocolo de comunicação.

## Compilação

Cada componente possui seu próprio `Makefile`. Para compilar, basta executar:

### Cliente

```bash
cd client
make
```

### Servidor

```bash
cd server
make
```

## Execução

### Servidor

O servidor escuta conexões na porta `12345` por padrão.

```bash
./server [-v] [-l prob]
```

**Parâmetros:**
- `-v` ou `--verbose`: ativa logs detalhados.
- `-l <prob>` ou `--loss <prob>`: define a probabilidade de perda de pacotes simulada (entre 0.0 e 1.0).

Exemplo:

```bash
./server -v -l 0.1
```

### Cliente

O cliente envia um arquivo para o servidor.

```bash
./client <arquivo> [-v] [-l prob]
```

**Parâmetros:**
- `<arquivo>`: caminho para o arquivo a ser enviado.
- `-v` ou `--verbose`: ativa logs detalhados.
- `-l <prob>` ou `--loss <prob>`: define a probabilidade de perda simulada.

Exemplo:

```bash
./client exemplo.txt -v -l 0.1
```

## Funcionalidades

- Comunicação via UDP com controle de confiabilidade.
- Suporte a simulação de perda de pacotes (dados e ACKs).
- Mecanismo de timeout e retransmissão.
- Modo detalhado de execução (`verbose`).
- Estatísticas ao final da execução (total de pacotes, retransmissões, perdas, etc.).

## Observações

- A simulação de perda é feita localmente nos códigos.
- A transferência é feita para `127.0.0.1:12345` por padrão (modifique `SERVER_IP` e `SERVER_PORT` em `client.c` se necessário).
- O protocolo implementa pacotes do tipo `START`, `DATA`, `ACK` e `EOT`.

## Autores

Oscar Alves
Wallace
Guilherme