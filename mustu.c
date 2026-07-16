/**
 * net_sender.c - Network Payload Sender (Burst Mode)
 * 
 * LEGITIMATE USE: Network testing, stress testing, performance analysis
 * Educational tool for understanding network protocols
 * 
 * COMPILE: gcc -o net_sender net_sender.c -lpthread
 * USAGE: ./net_sender <IP> <PORT> <TIME> <THREADS> <BURST_SIZE> <INTERVAL_US>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <sys/time.h>

// ========================= CONFIGURATION =========================
#define MAX_PAYLOAD 1024
#define MAX_THREADS 256
#define BUFFER_SIZE 4096
#define DEFAULT_BURST_SIZE 100
#define DEFAULT_INTERVAL_US 50

typedef struct {
    char target_ip[16];
    int target_port;
    int duration;           // Seconds to run
    int thread_id;
    volatile int *running;
    unsigned long long *packet_count;
    pthread_mutex_t *mutex;
    int burst_size;         // Packets per burst
    int interval_us;        // Microseconds between bursts
} ThreadData;

// ========================= GLOBALS =========================
volatile int global_running = 1;
pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned long long total_packets = 0;
unsigned long long total_bytes = 0;

// ========================= SIGNAL HANDLER =========================
void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        printf("\n\033[33m[!] Received interrupt signal. Stopping...\033[0m\n");
        global_running = 0;
    }
}

// ========================= PRINT USAGE =========================
void print_usage(char *program) {
    printf("\n\033[36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         NETWORK PAYLOAD SENDER v2.0 (BURST MODE)       ║\n");
    printf("║         Network Testing & Performance Tool             ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    printf("\033[33mUSAGE:\033[0m\n");
    printf("  %s \033[32m<IP> <PORT> <TIME> <THREADS> [BURST_SIZE] [INTERVAL_US]\033[0m\n\n", program);
    printf("\033[33mARGUMENTS:\033[0m\n");
    printf("  \033[32mIP\033[0m           - Target IP address (e.g., 192.168.1.1)\n");
    printf("  \033[32mPORT\033[0m         - Target port (1-65535)\n");
    printf("  \033[32mTIME\033[0m         - Duration in seconds (e.g., 10, 60, 300)\n");
    printf("  \033[32mTHREADS\033[0m      - Number of threads (1-%d)\n", MAX_THREADS);
    printf("  \033[32mBURST_SIZE\033[0m   - Packets per burst (default: %d)\n", DEFAULT_BURST_SIZE);
    printf("  \033[32mINTERVAL_US\033[0m  - Microseconds between bursts (default: %d)\n\n", DEFAULT_INTERVAL_US);
    printf("\033[33mEXAMPLES:\033[0m\n");
    printf("  %s 192.168.1.1 80 10 4                 # Default burst mode\n", program);
    printf("  %s 192.168.1.1 80 10 4 200 100         # High intensity\n", program);
    printf("  %s 8.8.8.8 53 30 8 500 50             # DNS server test\n\n", program);
    printf("\033[33mNOTE:\033[0m Press \033[32mCtrl+C\033[0m to stop anytime\n\n");
}

// ========================= CREATE UDP SOCKET =========================
int create_udp_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    // Increase buffer size for high throughput
    int buffer_size = 1024 * 1024; // 1MB
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    
    // Set non-blocking for high performance
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    return sock;
}

// ========================= GENERATE PAYLOAD =========================
void generate_payload(unsigned char *buffer, int size, int thread_id, int packet_num) {
    // Create high-intensity test payload
    const char *test_strings[] = {
        "BURST_TEST_",
        "PERF_TEST_",
        "STRESS_TEST_",
        "LOAD_TEST_"
    };
    
    int base_len = snprintf((char *)buffer, size, "[T%d][P%d] %s %ld", 
                            thread_id, packet_num, 
                            test_strings[packet_num % 4], 
                            time(NULL));
    
    // Fill remaining with random data for realistic payload
    if (base_len < size - 1) {
        for (int i = base_len; i < size - 1; i++) {
            buffer[i] = (rand() % 94) + 33;  // Printable ASCII
        }
        buffer[size - 1] = '\0';
    }
    
    // Ensure minimum packet size
    if (size < 64) {
        memset(buffer + base_len, 'X', 64 - base_len);
        buffer[63] = '\0';
    }
}

// ========================= HIGH PERFORMANCE SEND =========================
inline int send_udp_burst(int sock, unsigned char *payload, int payload_len, 
                          struct sockaddr_in *server_addr, int burst_size,
                          int thread_id, int *packet_num_ptr, 
                          unsigned long long *local_count) {
    int sent = 0;
    int errors = 0;
    
    // Send burst of packets
    for (int i = 0; i < burst_size; i++) {
        generate_payload(payload, MAX_PAYLOAD, thread_id, (*packet_num_ptr)++);
        int len = strlen((char *)payload);
        
        ssize_t result = sendto(sock, payload, len, MSG_DONTWAIT,
                               (struct sockaddr *)server_addr, sizeof(*server_addr));
        
        if (result > 0) {
            sent++;
            (*local_count)++;
        } else {
            errors++;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Socket buffer full, break burst
                break;
            }
        }
    }
    
    return sent;
}

// ========================= THREAD WORKER (BURST MODE) =========================
void *thread_worker(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    unsigned char payload[MAX_PAYLOAD];
    int udp_sock = -1;
    int packet_num = 0;
    struct sockaddr_in server_addr;
    struct timespec start_time, current_time, burst_start;
    int elapsed = 0;
    unsigned long long local_count = 0;
    int burst_size = data->burst_size > 0 ? data->burst_size : DEFAULT_BURST_SIZE;
    int interval_us = data->interval_us > 0 ? data->interval_us : DEFAULT_INTERVAL_US;
    
    // Create UDP socket
    udp_sock = create_udp_socket();
    if (udp_sock < 0) {
        printf("[THREAD %d] UDP socket creation failed\n", data->thread_id);
        pthread_exit(NULL);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->target_port);
    server_addr.sin_addr.s_addr = inet_addr(data->target_ip);
    
    // Set thread affinity for performance
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(data->thread_id % 8, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    printf("[THREAD %d] Started (Burst: %d, Interval: %dus)\n", 
           data->thread_id, burst_size, interval_us);
    
    while (*data->running && elapsed < data->duration) {
        // Send burst
        int sent = send_udp_burst(udp_sock, payload, MAX_PAYLOAD, 
                                  &server_addr, burst_size, 
                                  data->thread_id, &packet_num, &local_count);
        
        if (sent > 0) {
            pthread_mutex_lock(data->mutex);
            (*data->packet_count) += sent;
            total_bytes += (sent * (rand() % 64 + 64)); // Approximate bytes
            pthread_mutex_unlock(data->mutex);
        }
        
        // Calculate elapsed time
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        elapsed = current_time.tv_sec - start_time.tv_sec;
        
        // Microsecond precision delay between bursts
        if (interval_us > 0) {
            usleep(interval_us);
        }
    }
    
    if (udp_sock > 0) close(udp_sock);
    
    printf("[THREAD %d] Completed: %d packets sent (Local: %llu)\n", 
           data->thread_id, packet_num, local_count);
    pthread_exit(NULL);
}

// ========================= VALIDATE INPUT =========================
int validate_input(char *ip, int port, int time, int threads) {
    // Validate IP
    struct sockaddr_in sa;
    if (inet_pton(AF_INET, ip, &(sa.sin_addr)) != 1) {
        printf("\033[31m[!] Invalid IP address: %s\033[0m\n", ip);
        return 0;
    }
    
    // Validate port
    if (port < 1 || port > 65535) {
        printf("\033[31m[!] Invalid port: %d (must be 1-65535)\033[0m\n", port);
        return 0;
    }
    
    // Validate time
    if (time < 1 || time > 3600) {
        printf("\033[31m[!] Invalid time: %d (must be 1-3600 seconds)\033[0m\n", time);
        return 0;
    }
    
    // Validate threads
    if (threads < 1 || threads > MAX_THREADS) {
        printf("\033[31m[!] Invalid threads: %d (must be 1-%d)\033[0m\n", threads, MAX_THREADS);
        return 0;
    }
    
    return 1;
}

// ========================= REAL-TIME STATS =========================
void *stats_monitor(void *arg) {
    time_t start_time = time(NULL);
    unsigned long long last_count = 0;
    unsigned long long last_bytes = 0;
    int peak_rate = 0;
    
    while (global_running) {
        sleep(1);
        
        unsigned long long current_count = total_packets;
        unsigned long long current_bytes = total_bytes;
        unsigned long long rate = current_count - last_count;
        unsigned long long byte_rate = current_bytes - last_bytes;
        last_count = current_count;
        last_bytes = current_bytes;
        
        if (rate > peak_rate) peak_rate = rate;
        
        time_t now = time(NULL);
        int elapsed = now - start_time;
        
        // Clear line and print stats
        printf("\r\033[K");  // Clear line
        printf("\033[32m[STATS]\033[0m Packets: %llu | Rate: %llu pps | Bytes: %.2f MB | Peak: %d pps | Time: %ds", 
               current_count, rate, (float)byte_rate / (1024*1024), peak_rate, elapsed);
        fflush(stdout);
    }
    
    return NULL;
}

// ========================= MAIN FUNCTION =========================
int main(int argc, char *argv[]) {
    pthread_t threads[MAX_THREADS];
    pthread_t stats_thread;
    ThreadData thread_data[MAX_THREADS];
    int thread_count;
    char *target_ip;
    int target_port;
    int duration;
    int burst_size = DEFAULT_BURST_SIZE;
    int interval_us = DEFAULT_INTERVAL_US;
    
    // ====== ARGUMENT CHECK ======
    if (argc < 5 || argc > 7) {
        print_usage(argv[0]);
        return 1;
    }
    
    target_ip = argv[1];
    target_port = atoi(argv[2]);
    duration = atoi(argv[3]);
    thread_count = atoi(argv[4]);
    
    if (argc >= 6) {
        burst_size = atoi(argv[5]);
        if (burst_size < 1) burst_size = DEFAULT_BURST_SIZE;
    }
    
    if (argc >= 7) {
        interval_us = atoi(argv[6]);
        if (interval_us < 0) interval_us = DEFAULT_INTERVAL_US;
    }
    
    // ====== VALIDATE INPUT ======
    if (!validate_input(target_ip, target_port, duration, thread_count)) {
        return 1;
    }
    
    // ====== SETUP SIGNAL HANDLER ======
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // ====== BANNER ======
    printf("\n\033[36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║         NETWORK PAYLOAD SENDER v2.0 (BURST MODE)       ║\n");
    printf("║         Network Testing & Performance Tool             ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    printf("\033[33m[+] Target: \033[0m%s:%d\n", target_ip, target_port);
    printf("\033[33m[+] Duration: \033[0m%d seconds\n", duration);
    printf("\033[33m[+] Threads: \033[0m%d\n", thread_count);
    printf("\033[33m[+] Burst Size: \033[0m%d packets\n", burst_size);
    printf("\033[33m[+] Interval: \033[0m%d microseconds\n", interval_us);
    printf("\033[33m[+] Estimated Rate: \033[0m~%.0f pps\n", 
           (float)(thread_count * burst_size * 1000000) / interval_us);
    printf("\033[33m[+] Press Ctrl+C to stop early\033[0m\n\n");
    
    // ====== SEED RANDOM ======
    srand(time(NULL));
    
    // ====== CREATE THREADS ======
    for (int i = 0; i < thread_count; i++) {
        strcpy(thread_data[i].target_ip, target_ip);
        thread_data[i].target_port = target_port;
        thread_data[i].duration = duration;
        thread_data[i].thread_id = i + 1;
        thread_data[i].running = &global_running;
        thread_data[i].packet_count = &total_packets;
        thread_data[i].mutex = &global_mutex;
        thread_data[i].burst_size = burst_size;
        thread_data[i].interval_us = interval_us;
        
        if (pthread_create(&threads[i], NULL, thread_worker, &thread_data[i]) != 0) {
            printf("\033[31m[!] Failed to create thread %d\033[0m\n", i + 1);
            global_running = 0;
            break;
        }
    }
    
    // ====== START STATS MONITOR ======
    pthread_create(&stats_thread, NULL, stats_monitor, thread_data);
    
    // ====== WAIT FOR THREADS ======
    for (int i = 0; i < thread_count; i++) {
        pthread_join(threads[i], NULL);
    }
    
    global_running = 0;
    pthread_join(stats_thread, NULL);
    
    // ====== FINAL STATS ======
    printf("\n\n\033[36m");
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║                    FINAL STATISTICS                     ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    printf("\033[32m[+] Total Packets Sent: \033[0m%llu\n", total_packets);
    printf("\033[32m[+] Total Data Sent: \033[0m%.2f MB\n", (float)total_bytes / (1024*1024));
    printf("\033[32m[+] Average Rate: \033[0m%.2f pps\n", 
           (float)total_packets / duration);
    printf("\033[32m[+] Duration: \033[0m%d seconds\n", duration);
    printf("\033[32m[+] Threads Used: \033[0m%d\n", thread_count);
    printf("\033[32m[+] Burst Size: \033[0m%d\n", burst_size);
    printf("\n\033[33m[✓] Test completed successfully!\033[0m\n\n");
    
    return 0;
}
