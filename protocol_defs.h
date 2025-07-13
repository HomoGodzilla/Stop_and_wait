// protocol_defs.h
#ifndef PROTOCOL_DEFS_H
#define PROTOCOL_DEFS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <string.h> // Para memcpy

// Definições de tipos de pacote
#define PKT_DATA 0x01 // Pacote de dados
#define PKT_ACK  0x02 // Pacote de ACK
#define PKT_EOT  0x03 // Pacote de Fim de Transmissão (End of Transmission)

#define MAX_PAYLOAD_SIZE 1024 // Tamanho máximo do payload de dados

// Estrutura do cabeçalho do pacote
typedef struct {
    uint8_t  type;
    uint8_t  sequence_num;
    uint16_t length;
    uint16_t checksum;
} PacketHeader;

// Estrutura completa do pacote
typedef struct {
    PacketHeader header;
    char         payload[MAX_PAYLOAD_SIZE];
} Packet;

// Estrutura para ACK
typedef struct {
    uint8_t type;
    uint8_t sequence_num;
} ACKPacket;

// Protótipos das funções auxiliares
uint16_t calculate_checksum(const char *data, size_t length);
void init_random();
bool simulate_loss();

// Implementações inline ou em um .c separado se preferir
// Para simplificar, vou colocar aqui (não é a melhor prática para projetos grandes)
static inline uint16_t calculate_checksum(const char *data, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i++) {
        sum += (uint8_t)data[i];
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (uint16_t)~sum;
}

static inline void init_random() {
    srand(time(NULL));
}

static double LOSS_PROBABILITY = 0.1; // Ajuste conforme necessário

static inline bool simulate_loss() {
    double r = (double)rand() / RAND_MAX;
    return r < LOSS_PROBABILITY;
}

#endif // PROTOCOL_DEFS_H