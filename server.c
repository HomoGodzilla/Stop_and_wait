#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>

// Inclua aqui as definições de Packet, ACKPacket, etc. e as funções auxiliares
#include "protocol_defs.h" // Arquivo com as structs e funções auxiliares

#define SERVER_PORT 12345
#define BUFFER_SIZE (sizeof(Packet))

int main() {
    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    FILE *output_file;

    // Para estatísticas
    long long total_packets_received = 0;
    long long duplicate_packets = 0;

    // Para controle de sequência do servidor
    uint8_t expected_sequence = 0;

    init_random(); // Inicializa para simulação de perda

    // 1. Criar socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    // Configurar endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Aceita de qualquer IP
    server_addr.sin_port = htons(SERVER_PORT);

    // 2. Vincular socket ao endereço e porta
    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor UDP ouvindo na porta %d...\n", SERVER_PORT);

    // Abrir arquivo para escrita (Ex: "received_file.txt")
    output_file = fopen("received_file.txt", "wb");
    if (!output_file) {
        perror("Error opening output file");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    Packet received_pkt;
    ACKPacket ack_pkt;
    char ack_buffer[sizeof(ACKPacket)];

    while (true) {
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                             (struct sockaddr *)&client_addr, &addr_len);

        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }

        // Simular perda de pacote recebido (opcional, mas bom para testes)
        if (simulate_loss()) {
            printf("[SERVER] Simulating loss of received packet (seq: %d).\n", expected_sequence);
            continue; // age como se nunca tivesse recebido o pacote
        }

        // Deserializar o buffer para a estrutura do pacote
        // Você precisará de uma função para isso, e.g., deserialize_packet(buffer, &received_pkt);
        // Exemplo simplificado:
        memcpy(&received_pkt, buffer, n);

        // Verificar o checksum (se implementado)
        uint16_t calculated_chksum = calculate_checksum(received_pkt.payload, received_pkt.header.length);
        if (calculated_chksum != received_pkt.header.checksum) {
            printf("[SERVER] Pacote corrompido detectado (seq: %d). Descartando.\n", received_pkt.header.sequence_num);
            // Mesmo que corrompido, pode ser útil enviar um ACK para o último pacote bom
            // Para Stop-and-Wait, o cliente retransmitirá se não receber ACK
            continue;
        }

        total_packets_received++;

        if (received_pkt.header.type == PKT_DATA) {
            if (received_pkt.header.sequence_num == expected_sequence) {
                printf("[SERVER] Recebido pacote de DADOS (seq: %d, len: %d).\n",
                       received_pkt.header.sequence_num, received_pkt.header.length);

                // Escrever no arquivo
                fwrite(received_pkt.payload, 1, received_pkt.header.length, output_file);

                // Inverter o número de sequência esperado para o próximo
                expected_sequence = 1 - expected_sequence;

            } else {
                // Pacote duplicado ou fora de ordem (para Stop-and-Wait, só duplicado)
                printf("[SERVER] Recebido pacote duplicado ou fora de ordem (seq: %d). Esperava %d. Descartando.\n",
                       received_pkt.header.sequence_num, expected_sequence);
                duplicate_packets++;
                // Mesmo se duplicado, enviar ACK para o pacote esperado (o que já foi recebido)
                // Isso informa ao cliente que o ACK foi perdido e ele pode parar a retransmissão
            }

            // Enviar ACK para o pacote recebido (seja ele esperado ou duplicado)
            // O ACK deve ser para o pacote que o cliente *enviou*, não o que esperamos
            // No Stop-and-Wait, isso geralmente significa enviar o ACK do seq_num do pacote recebido
            ack_pkt.type = PKT_ACK;
            ack_pkt.sequence_num = received_pkt.header.sequence_num; // ACK para o pacote que chegou

            // Serializar o ACK para o buffer
            memcpy(ack_buffer, &ack_pkt, sizeof(ACKPacket)); // Simplificado

            if (simulate_loss()) {
                printf("[SERVER] Simulating loss of ACK (seq: %d).\n", ack_pkt.sequence_num);
            } else {
                sendto(sockfd, ack_buffer, sizeof(ACKPacket), 0,
                       (const struct sockaddr *)&client_addr, addr_len);
                printf("[SERVER] Enviado ACK para pacote (seq: %d).\n", ack_pkt.sequence_num);
            }
        } else if (received_pkt.header.type == PKT_EOT) {
            printf("[SERVER] Recebido pacote de FIM DE TRANSMISSÃO.\n");
            // Enviar ACK para EOT (opcional, dependendo do design)
            ack_pkt.type = PKT_ACK;
            ack_pkt.sequence_num = received_pkt.header.sequence_num; // Pode ser 0 ou 1, não importa
            memcpy(ack_buffer, &ack_pkt, sizeof(ACKPacket));

            sendto(sockfd, ack_buffer, sizeof(ACKPacket), 0,
                   (const struct sockaddr *)&client_addr, addr_len);
            printf("[SERVER] Enviado ACK para EOT.\n");
            break; // Sair do loop principal
        }
    }

    fclose(output_file);
    close(sockfd);

    printf("\n--- Estatísticas do Servidor ---\n");
    printf("Total de pacotes de dados recebidos: %lld\n", total_packets_received);
    printf("Pacotes duplicados descartados: %lld\n", duplicate_packets);
    printf("Arquivo 'received_file.txt' criado com sucesso.\n");

    return 0;
}