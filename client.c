#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h> // Para EWOULDBLOCK

// Inclua aqui as definições de Packet, ACKPacket, etc. e as funções auxiliares
#include "protocol_defs.h" // Arquivo com as structs e funções auxiliares

#define SERVER_IP "127.0.0.1" // IP do servidor
#define SERVER_PORT 12345
#define TIMEOUT_SEC 2       // Tempo limite de espera por ACK em segundos
#define MAX_RETRIES 5       // Número máximo de retransmissões

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Uso: %s <caminho_do_arquivo>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in server_addr;
    char buffer[sizeof(Packet)];
    FILE *input_file;

    // Para estatísticas
    long long total_packets_sent = 0;
    long long total_retransmissions = 0;
    time_t start_time, end_time;

    // Para controle de sequência do cliente
    uint8_t sequence_to_send = 0;

    init_random(); // Inicializa para simulação de perda

    // 1. Criar socket UDP
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));

    // Configurar endereço do servidor
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Configurar timeout para o socket (para recvfrom)
    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // Abrir arquivo para leitura
    input_file = fopen(argv[1], "rb");
    if (!input_file) {
        perror("Error opening input file");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Iniciando transferência do arquivo '%s' para %s:%d...\n",
           argv[1], SERVER_IP, SERVER_PORT);
    time(&start_time); // Inicia o timer de transferência

    Packet data_pkt;
    ACKPacket ack_pkt;
    ssize_t bytes_read;
    int retries;

    while (true) {
        // Preparar o pacote de dados
        data_pkt.header.type = PKT_DATA;
        data_pkt.header.sequence_num = sequence_to_send;

        // Ler um bloco do arquivo
        bytes_read = fread(data_pkt.payload, 1, MAX_PAYLOAD_SIZE, input_file);
        data_pkt.header.length = bytes_read;

        if (bytes_read == 0 && !feof(input_file)) {
            perror("Error reading file");
            break;
        }

        // Calcular checksum (se implementado)
        data_pkt.header.checksum = calculate_checksum(data_pkt.payload, data_pkt.header.length);

        // Serializar o pacote para o buffer
        memcpy(buffer, &data_pkt, sizeof(PacketHeader) + data_pkt.header.length); // Simplificado

        retries = 0;
        bool ack_received = false;

        do {
            printf("[CLIENT] Enviando pacote de DADOS (seq: %d, len: %d). Retries: %d\n",
                   data_pkt.header.sequence_num, data_pkt.header.length, retries);

            if (simulate_loss()) {
                printf("[CLIENT] Simulating loss of DATA packet (seq: %d).\n", data_pkt.header.sequence_num);
                // Não enviamos, mas agimos como se tivéssemos enviado para o timer expirar
            } else {
                sendto(sockfd, buffer, sizeof(PacketHeader) + data_pkt.header.length, 0,
                       (const struct sockaddr *)&server_addr, sizeof(server_addr));
            }
            total_packets_sent++;

            // Esperar por ACK
            char ack_recv_buffer[sizeof(ACKPacket)];
            ssize_t n_ack = recvfrom(sockfd, ack_recv_buffer, sizeof(ACKPacket), 0, NULL, NULL);

            if (n_ack < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    printf("[CLIENT] TIMEOUT! Nenhum ACK recebido para pacote (seq: %d).\n",
                           data_pkt.header.sequence_num);
                    total_retransmissions++;
                    retries++;
                } else {
                    perror("recvfrom ACK failed");
                    break;
                }
            } else {
                // Simular perda de ACK recebido no cliente
                if (simulate_loss()) {
                    printf("[CLIENT] Simulating loss of received ACK (seq: %d).\n", sequence_to_send);
                    total_retransmissions++;
                    retries++;
                    continue; // Pular para próxima tentativa se ACK for perdido
                }

                // Deserializar o ACK
                memcpy(&ack_pkt, ack_recv_buffer, n_ack); // Simplificado

                if (ack_pkt.type == PKT_ACK && ack_pkt.sequence_num == sequence_to_send) {
                    printf("[CLIENT] ACK recebido para pacote (seq: %d).\n", ack_pkt.sequence_num);
                    ack_received = true;
                } else {
                    printf("[CLIENT] ACK inesperado recebido (seq: %d, tipo: %d). Esperava seq: %d. Retransmitindo.\n",
                           ack_pkt.sequence_num, ack_pkt.type, sequence_to_send);
                    total_retransmissions++;
                    retries++;
                }
            }
        } while (!ack_received && retries < MAX_RETRIES);

        if (!ack_received) {
            fprintf(stderr, "ERRO: Número máximo de retransmissões excedido para pacote (seq: %d). Abortando.\n",
                    data_pkt.header.sequence_num);
            break;
        }

        // Inverter o número de sequência para o próximo pacote
        sequence_to_send = 1 - sequence_to_send;

        // Se chegamos ao fim do arquivo, sair do loop
        if (feof(input_file)) {
            break;
        }
    }

    // Enviar pacote de FIM DE TRANSMISSÃO (EOT)
    // O EOT também deve ser confiável, então implementamos um loop de retransmissão similar
    data_pkt.header.type = PKT_EOT;
    data_pkt.header.sequence_num = sequence_to_send; // O último sequence_to_send usado
    data_pkt.header.length = 0; // Nenhum dado
    data_pkt.header.checksum = 0; // Ou calcule um checksum para o header

    memcpy(buffer, &data_pkt, sizeof(PacketHeader)); // Serializar apenas o cabeçalho

    retries = 0;
    bool eot_acked = false;
    do {
        printf("[CLIENT] Enviando pacote EOT (seq: %d). Retries: %d\n", data_pkt.header.sequence_num, retries);
        if (simulate_loss()) {
            printf("[CLIENT] Simulating loss of EOT packet.\n");
        } else {
            sendto(sockfd, buffer, sizeof(PacketHeader), 0,
                   (const struct sockaddr *)&server_addr, sizeof(server_addr));
        }
        total_packets_sent++; // Conta o EOT também

        char ack_recv_buffer[sizeof(ACKPacket)];
        ssize_t n_ack = recvfrom(sockfd, ack_recv_buffer, sizeof(ACKPacket), 0, NULL, NULL);

        if (n_ack < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                printf("[CLIENT] TIMEOUT! Nenhum ACK recebido para EOT (seq: %d).\n",
                       data_pkt.header.sequence_num);
                total_retransmissions++;
                retries++;
            } else {
                perror("recvfrom ACK for EOT failed");
                break;
            }
        } else {
            if (simulate_loss()) {
                printf("[CLIENT] Simulating loss of received ACK for EOT.\n");
                total_retransmissions++;
                retries++;
                continue;
            }

            memcpy(&ack_pkt, ack_recv_buffer, n_ack);
            if (ack_pkt.type == PKT_ACK && ack_pkt.sequence_num == data_pkt.header.sequence_num) {
                printf("[CLIENT] ACK de EOT recebido para (seq: %d).\n", ack_pkt.sequence_num);
                eot_acked = true;
            } else {
                 printf("[CLIENT] ACK inesperado para EOT (seq: %d). Retransmitindo EOT.\n", ack_pkt.sequence_num);
                 total_retransmissions++;
                 retries++;
            }
        }
    } while (!eot_acked && retries < MAX_RETRIES);

    if (!eot_acked) {
        fprintf(stderr, "AVISO: Falha ao confirmar EOT após retransmissões.\n");
    }

    fclose(input_file);
    close(sockfd);

    time(&end_time); // Finaliza o timer de transferência

    printf("\n--- Estatísticas do Cliente ---\n");
    printf("Tempo total de transferência: %.2f segundos\n", difftime(end_time, start_time));
    printf("Total de pacotes enviados (incl. EOT): %lld\n", total_packets_sent);
    printf("Total de retransmissões: %lld\n", total_retransmissions);
    printf("Taxa de retransmissão: %.2f%%\n", (double)total_retransmissions / (total_packets_sent - 1) * 100); // Sem contar EOT na base
    printf("Transferência concluída.\n");

    return 0;
}