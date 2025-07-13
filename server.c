// server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <getopt.h>

#include "protocol_defs.h"

#define SERVER_PORT 12345
#define BUFFER_SIZE (sizeof(Packet))

// Variáveis globais para configuração
bool verbose_mode = false;
double loss_probability = 0.0;

void print_usage(const char *prog_name) {
    fprintf(stderr, "Uso: %s [-v] [-l prob]\n", prog_name);
    fprintf(stderr, "  -v, --verbose          Ativa o modo de log detalhado.\n");
    fprintf(stderr, "  -l, --loss <prob>      Define a probabilidade de perda (0.0 a 1.0).\n");
}

// Wrapper para logs verbosos
void verbose_log(const char *format, ...) {
    if (verbose_mode) {
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        va_end(args);
    }
}

int main(int argc, char *argv[]) {
    // Parsing de argumentos da linha de comando
    const struct option long_options[] = {
        {"verbose", no_argument, 0, 'v'},
        {"loss", required_argument, 0, 'l'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "vl:", long_options, NULL)) != -1) {
        switch (opt) {
            case 'v':
                verbose_mode = true;
                break;
            case 'l':
                loss_probability = atof(optarg);
                if (loss_probability < 0.0 || loss_probability > 1.0) {
                    fprintf(stderr, "Erro: A probabilidade de perda deve ser entre 0.0 e 1.0\n");
                    return EXIT_FAILURE;
                }
                break;
            default:
                print_usage(argv[0]);
                return EXIT_FAILURE;
        }
    }

    int sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    FILE *output_file = NULL;

    long long total_packets_received = 0;
    long long duplicate_packets = 0;
    long long corrupted_packets = 0;

    init_random();

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor UDP ouvindo na porta %d...\n", SERVER_PORT);
    if(verbose_mode) printf("Modo Verbose Ativado. Probabilidade de Perda: %.2f%%\n", loss_probability * 100);

    bool receiving_data = false;
    uint8_t expected_sequence = 0;

    while (true) {
        ssize_t n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                             (struct sockaddr *)&client_addr, &addr_len);

        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }
        
        if (simulate_loss(loss_probability)) {
            verbose_log("[SERVER] >> Simulação de perda de pacote recebido.\n");
            continue;
        }

        PacketHeader *header = (PacketHeader *)buffer;
        
        // Tratar pacotes START
        if (header->type == PKT_START) {
            StartPacket *start_pkt = (StartPacket *)buffer;
            uint16_t calculated_chksum = calculate_checksum(start_pkt->filename, start_pkt->header.length);

            if (calculated_chksum != start_pkt->header.checksum) {
                verbose_log("[SERVER] Pacote START corrompido. Descartando.\n");
                corrupted_packets++;
                continue;
            }

            char filename[MAX_FILENAME_SIZE + 1];
            strncpy(filename, start_pkt->filename, start_pkt->header.length);
            filename[start_pkt->header.length] = '\0';
            
            printf("Recebendo arquivo: %s\n", filename);

            output_file = fopen(filename, "wb");
            if (!output_file) {
                perror("Error opening output file");
                close(sockfd);
                exit(EXIT_FAILURE);
            }

            // Enviar ACK para START
            ACKPacket ack_pkt;
            ack_pkt.type = PKT_ACK;
            ack_pkt.sequence_num = 0; // Não relevante para START
            sendto(sockfd, &ack_pkt, sizeof(ACKPacket), 0, (const struct sockaddr *)&client_addr, addr_len);
            verbose_log("[SERVER] Enviado ACK para START.\n");
            
            receiving_data = true;
            expected_sequence = 0; // Inicia a sequência de dados
            continue;
        }

        if (!receiving_data) {
            verbose_log("[SERVER] Aguardando pacote START. Pacote recebido descartado.\n");
            continue;
        }
        
        // Tratar pacotes de DADOS
        if (header->type == PKT_DATA) {
            Packet *data_pkt = (Packet *)buffer;
            uint16_t calculated_chksum = calculate_checksum(data_pkt->payload, data_pkt->header.length);

            if (calculated_chksum != data_pkt->header.checksum) {
                verbose_log("[SERVER] Pacote de DADOS corrompido (seq: %d). Descartando.\n", data_pkt->header.sequence_num);
                corrupted_packets++;
                continue;
            }
            
            total_packets_received++;
            
            if (data_pkt->header.sequence_num == expected_sequence) {
                verbose_log("[SERVER] Recebido pacote de DADOS (seq: %d, len: %u).\n",
                       data_pkt->header.sequence_num, data_pkt->header.length);

                fwrite(data_pkt->payload, 1, data_pkt->header.length, output_file);
                expected_sequence = 1 - expected_sequence;

            } else {
                verbose_log("[SERVER] Pacote duplicado (seq: %d). Esperava %d. Descartando.\n",
                       data_pkt->header.sequence_num, expected_sequence);
                duplicate_packets++;
            }

            // Sempre envia ACK para o pacote que chegou, para o cliente não ficar em timeout
            ACKPacket ack_pkt;
            ack_pkt.type = PKT_ACK;
            ack_pkt.sequence_num = data_pkt->header.sequence_num;

            if (!simulate_loss(loss_probability)) {
                sendto(sockfd, &ack_pkt, sizeof(ACKPacket), 0, (const struct sockaddr *)&client_addr, addr_len);
                verbose_log("[SERVER] Enviado ACK para pacote (seq: %d).\n", ack_pkt.sequence_num);
            } else {
                verbose_log("[SERVER] >> Simulação de perda do ACK (para seq: %d).\n", ack_pkt.sequence_num);
            }

        } else if (header->type == PKT_EOT) {
            verbose_log("[SERVER] Recebido pacote de FIM DE TRANSMISSÃO.\n");
            
            // Enviar ACK para EOT
            ACKPacket ack_pkt;
            ack_pkt.type = PKT_ACK;
            ack_pkt.sequence_num = header->sequence_num;
            sendto(sockfd, &ack_pkt, sizeof(ACKPacket), 0, (const struct sockaddr *)&client_addr, addr_len);
            verbose_log("[SERVER] Enviado ACK para EOT (seq: %d).\n", ack_pkt.sequence_num);

            break; // Sair do loop
        }
    }

    if(output_file) fclose(output_file);
    close(sockfd);

    printf("\n--- Estatísticas do Servidor ---\n");
    printf("Transferência finalizada.\n");
    printf("Total de pacotes de dados recebidos: %lld\n", total_packets_received);
    printf("Pacotes duplicados descartados: %lld\n", duplicate_packets);
    printf("Pacotes corrompidos descartados: %lld\n", corrupted_packets);
    printf("-----------------------------------\n");


    return 0;
}