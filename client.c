// client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <libgen.h> // Para basename()

#include "protocol_defs.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define TIMEOUT_SEC 2
#define MAX_RETRIES 5

// Variáveis globais para configuração
bool verbose_mode = false;
double loss_probability = 0.0;

void print_usage(const char *prog_name) {
    fprintf(stderr, "Uso: %s <caminho_do_arquivo> [-v] [-l prob]\n", prog_name);
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
    char *filepath = NULL;

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

    if (optind >= argc) {
        fprintf(stderr, "Erro: Caminho do arquivo não especificado.\n");
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
    filepath = argv[optind];

    int sockfd;
    struct sockaddr_in server_addr;
    FILE *input_file;

    long long total_packets_sent = 0;
    long long total_retransmissions = 0;
    time_t start_time, end_time;

    init_random();

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        perror("Invalid address/ Address not supported");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    struct timeval tv;
    tv.tv_sec = TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
        perror("setsockopt failed");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    input_file = fopen(filepath, "rb");
    if (!input_file) {
        perror("Error opening input file");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    
    char* filename = basename(filepath);

    printf("Iniciando transferência do arquivo '%s' para %s:%d...\n", filename, SERVER_IP, SERVER_PORT);
    if(verbose_mode) printf("Modo Verbose Ativado. Probabilidade de Perda: %.2f%%\n", loss_probability * 100);

    time(&start_time);

    // 1. Enviar pacote de START de forma confiável
    StartPacket start_pkt;
    start_pkt.header.type = PKT_START;
    start_pkt.header.length = strlen(filename);
    strncpy(start_pkt.filename, filename, MAX_FILENAME_SIZE);
    start_pkt.filename[MAX_FILENAME_SIZE] = '\0';
    start_pkt.header.checksum = calculate_checksum(start_pkt.filename, start_pkt.header.length);

    int retries = 0;
    bool start_acked = false;
    do {
        verbose_log("[CLIENT] Enviando pacote START para o arquivo '%s'. Tentativa: %d\n", filename, retries + 1);
        
        if (!simulate_loss(loss_probability)) {
            sendto(sockfd, &start_pkt, sizeof(PacketHeader) + start_pkt.header.length, 0,
                   (const struct sockaddr *)&server_addr, sizeof(server_addr));
        } else {
             verbose_log("[CLIENT] >> Simulação de perda do pacote START.\n");
        }
        total_packets_sent++;

        char ack_recv_buffer[sizeof(ACKPacket)];
        ssize_t n_ack = recvfrom(sockfd, ack_recv_buffer, sizeof(ACKPacket), 0, NULL, NULL);

        if (n_ack > 0) {
            ACKPacket ack_pkt;
            memcpy(&ack_pkt, ack_recv_buffer, n_ack);
            if (ack_pkt.type == PKT_ACK) { // ACK para START não tem sequence_num
                verbose_log("[CLIENT] ACK para START recebido.\n");
                start_acked = true;
            }
        } else {
            verbose_log("[CLIENT] TIMEOUT! Nenhum ACK recebido para o pacote START.\n");
            total_retransmissions++;
            retries++;
        }
    } while (!start_acked && retries < MAX_RETRIES);

    if (!start_acked) {
        fprintf(stderr, "ERRO: Servidor não respondeu ao início da transmissão. Abortando.\n");
        fclose(input_file);
        close(sockfd);
        return EXIT_FAILURE;
    }


    // 2. Enviar dados do arquivo
    Packet data_pkt;
    ACKPacket ack_pkt;
    ssize_t bytes_read;
    uint8_t sequence_to_send = 0;

    while (true) {
        bytes_read = fread(data_pkt.payload, 1, MAX_PAYLOAD_SIZE, input_file);
        
        if (bytes_read == 0 && feof(input_file)) {
            break; // Fim do arquivo
        }
        if (bytes_read == 0 && !feof(input_file)) {
            perror("Error reading file");
            break;
        }

        data_pkt.header.type = PKT_DATA;
        data_pkt.header.sequence_num = sequence_to_send;
        data_pkt.header.length = bytes_read;
        data_pkt.header.checksum = calculate_checksum(data_pkt.payload, data_pkt.header.length);

        retries = 0;
        bool ack_received = false;
        do {
            verbose_log("[CLIENT] Enviando pacote de DADOS (seq: %d, len: %u). Tentativa: %d\n",
                   data_pkt.header.sequence_num, data_pkt.header.length, retries + 1);

            if (!simulate_loss(loss_probability)) {
                sendto(sockfd, &data_pkt, sizeof(PacketHeader) + data_pkt.header.length, 0,
                       (const struct sockaddr *)&server_addr, sizeof(server_addr));
            } else {
                verbose_log("[CLIENT] >> Simulação de perda do pacote de DADOS (seq: %d).\n", data_pkt.header.sequence_num);
            }
            total_packets_sent++;

            char ack_recv_buffer[sizeof(ACKPacket)];
            ssize_t n_ack = recvfrom(sockfd, ack_recv_buffer, sizeof(ACKPacket), 0, NULL, NULL);

            if (n_ack < 0) {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    verbose_log("[CLIENT] TIMEOUT! Nenhum ACK para pacote (seq: %d).\n", data_pkt.header.sequence_num);
                    total_retransmissions++;
                    retries++;
                } else {
                    perror("recvfrom ACK failed");
                    break;
                }
            } else {
                if (simulate_loss(loss_probability)) {
                    verbose_log("[CLIENT] >> Simulação de perda do ACK recebido para (seq: %d).\n", sequence_to_send);
                    total_retransmissions++;
                    retries++;
                    continue;
                }
                
                memcpy(&ack_pkt, ack_recv_buffer, n_ack);

                if (ack_pkt.type == PKT_ACK && ack_pkt.sequence_num == sequence_to_send) {
                    verbose_log("[CLIENT] ACK recebido para pacote (seq: %d).\n", ack_pkt.sequence_num);
                    ack_received = true;
                } else {
                    verbose_log("[CLIENT] ACK inesperado (seq: %d, tipo: %d). Esperava seq: %d. Retransmitindo.\n",
                           ack_pkt.sequence_num, ack_pkt.type, sequence_to_send);
                    total_retransmissions++;
                    retries++;
                }
            }
        } while (!ack_received && retries < MAX_RETRIES);

        if (!ack_received) {
            fprintf(stderr, "ERRO: Máximo de retransmissões excedido para pacote (seq: %d). Abortando.\n",
                    data_pkt.header.sequence_num);
            goto cleanup;
        }

        sequence_to_send = 1 - sequence_to_send;
    }

    // 3. Enviar pacote de FIM DE TRANSMISSÃO (EOT)
    data_pkt.header.type = PKT_EOT;
    data_pkt.header.sequence_num = sequence_to_send;
    data_pkt.header.length = 0;
    data_pkt.header.checksum = 0;

    retries = 0;
    bool eot_acked = false;
    do {
        verbose_log("[CLIENT] Enviando pacote EOT (seq: %d). Tentativa: %d\n", data_pkt.header.sequence_num, retries + 1);
        if (!simulate_loss(loss_probability)) {
            sendto(sockfd, &data_pkt.header, sizeof(PacketHeader), 0,
                   (const struct sockaddr *)&server_addr, sizeof(server_addr));
        } else {
            verbose_log("[CLIENT] >> Simulação de perda do pacote EOT.\n");
        }
        total_packets_sent++;

        char ack_recv_buffer[sizeof(ACKPacket)];
        ssize_t n_ack = recvfrom(sockfd, ack_recv_buffer, sizeof(ACKPacket), 0, NULL, NULL);

        if (n_ack < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                verbose_log("[CLIENT] TIMEOUT! Nenhum ACK para EOT (seq: %d).\n", data_pkt.header.sequence_num);
                total_retransmissions++;
                retries++;
            } else {
                perror("recvfrom ACK for EOT failed");
                break;
            }
        } else {
             if (simulate_loss(loss_probability)) {
                verbose_log("[CLIENT] >> Simulação de perda do ACK para EOT.\n");
                total_retransmissions++;
                retries++;
                continue;
            }
            
            memcpy(&ack_pkt, ack_recv_buffer, n_ack);
            if (ack_pkt.type == PKT_ACK && ack_pkt.sequence_num == data_pkt.header.sequence_num) {
                verbose_log("[CLIENT] ACK de EOT recebido (seq: %d).\n", ack_pkt.sequence_num);
                eot_acked = true;
            } else {
                 verbose_log("[CLIENT] ACK inesperado para EOT (seq: %d). Retransmitindo.\n", ack_pkt.sequence_num);
                 total_retransmissions++;
                 retries++;
            }
        }
    } while (!eot_acked && retries < MAX_RETRIES);

    if (!eot_acked) {
        fprintf(stderr, "AVISO: Falha ao confirmar EOT após retransmissões.\n");
    }

cleanup:
    fclose(input_file);
    close(sockfd);

    time(&end_time);
    double total_time = difftime(end_time, start_time);
    if (total_time < 1) total_time = 1; // Evitar divisão por zero

    printf("\n--- Estatísticas do Cliente ---\n");
    printf("Transferência concluída.\n");
    printf("Tempo total de transferência: %.2f segundos\n", total_time);
    printf("Total de pacotes (START/DATA/EOT) enviados: %lld\n", total_packets_sent);
    printf("Total de retransmissões: %lld\n", total_retransmissions);
    if (total_packets_sent > 1) {
       printf("Taxa de retransmissão: %.2f%%\n", (double)total_retransmissions / (total_packets_sent) * 100);
    }
    printf("----------------------------------\n");

    return 0;
}