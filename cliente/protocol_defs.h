// protocol_defs.h
#ifndef PROTOCOL_DEFS_H
#define PROTOCOL_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <string.h> // Para memcpy

// Definições de tipos de pacote
#define PKT_DATA  0x01 // Pacote de dados
#define PKT_ACK   0x02 // Pacote de ACK
#define PKT_EOT   0x03 // Pacote de Fim de Transmissão (End of Transmission)
#define PKT_START 0x04 // Pacote de Início de Transmissão

#define MAX_PAYLOAD_SIZE 1024 // Tamanho máximo do payload de dados
#define MAX_FILENAME_SIZE 255 // Tamanho máximo para nome de arquivo

// Estrutura do cabeçalho do pacote
typedef struct {
    uint8_t  type;
    uint8_t  sequence_num;
    uint16_t length;
    uint16_t checksum;
} PacketHeader;

// Estrutura completa do pacote de dados
typedef struct {
    PacketHeader header;
    char         payload[MAX_PAYLOAD_SIZE];
} Packet;

// Estrutura para pacote de START (com nome do arquivo)
typedef struct {
    PacketHeader header;
    char filename[MAX_FILENAME_SIZE + 1];
} StartPacket;

// Estrutura para ACK
typedef struct {
    uint8_t type;
    uint8_t sequence_num;
} ACKPacket;

// Implementações
static inline uint16_t calculate_checksum(const void *data, size_t length) {
    uint32_t sum = 0;
    const uint8_t* bytes = (const uint8_t*)data;
    for (size_t i = 0; i < length; i++) {
        sum += bytes[i];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static inline void init_random() {
    srand(time(NULL));
}

// Agora aceita a probabilidade como argumento
static inline bool simulate_loss(double probability) {
    if (probability <= 0.0) return false;
    double r = (double)rand() / RAND_MAX;
    return r < probability;
}

#endif // PROTOCOL_DEFS_H