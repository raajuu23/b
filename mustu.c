/**
 * net_sender.c - Network Payload Sender
 * 
 * LEGITIMATE USE: Network testing, stress testing, performance analysis
 * Educational tool for understanding network protocols
 * 
 * COMPILE: gcc -o net_sender net_sender.c -lpthread
 * USAGE: ./net_sender <IP> <PORT> <TIME> <THREADS>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

// ========================= CONFIGURATION =========================
#define MAX_PAYLOAD 1024
#define MAX_THREADS 256
#define BUFFER_SIZE 4096

typedef struct {
    char target_ip[16];
    int target_port;
    int duration;           // Seconds to run
    int thread_id;
    volatile int *running;
    unsigned long long *packet_count;
    pthread_mutex_t *mutex;
} ThreadData;

// ========================= GLOBALS =========================
volatile int global_running = 1;
pthread_mutex_t global_mutex = PTHREAD_MUTEX_INITIALIZER;
unsigned long long total_packets = 0;

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
    printf("║         NETWORK PAYLOAD SENDER v1.0                    ║\n");
    printf("║         Network Testing & Performance Tool             ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    printf("\033[33mUSAGE:\033[0m\n");
    printf("  %s \033[32m<IP> <PORT> <TIME> <THREADS>\033[0m\n\n", program);
    printf("\033[33mARGUMENTS:\033[0m\n");
    printf("  \033[32mIP\033[0m       - Target IP address (e.g., 192.168.1.1)\n");
    printf("  \033[32mPORT\033[0m     - Target port (1-65535)\n");
    printf("  \033[32mTIME\033[0m     - Duration in seconds (e.g., 10, 60, 300)\n");
    printf("  \033[32mTHREADS\033[0m  - Number of threads (1-%d)\n\n", MAX_THREADS);
    printf("\033[33mEXAMPLES:\033[0m\n");
    printf("  %s 192.168.1.1 80 10 4         # 4 threads, 10 seconds\n", program);
    printf("  %s 8.8.8.8 53 30 8             # DNS server test\n", program);
    printf("  %s 127.0.0.1 8080 5 2          # Localhost test\n\n");
    printf("\033[33mNOTE:\033[0m Press \033[32mCtrl+C\033[0m to stop anytime\n\n");
}

// ========================= CREATE TCP SOCKET =========================
int create_tcp_socket(const char *ip, int port) {
    int sock;
    struct sockaddr_in server_addr;
    
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }
    
    // Set socket timeout
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr(ip);
    
    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

// ========================= CREATE UDP SOCKET =========================
int create_udp_socket() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        return -1;
    }
    return sock;
}

// ========================= GENERATE PAYLOAD =========================
void generate_payload(unsigned char *buffer, int size, int thread_id, int packet_num) {
    // Create meaningful test payload
    const char *test_strings[] = {
        "GET / HTTP/1.1\r\nHost: test\r\n\r\n",
        "HEAD / HTTP/1.1\r\nHost: test\r\n\r\n",
        "PING",
        "TEST_DATA_",
        "NETWORK_TEST_"
    };
    
    int base_len = snprintf((char *)buffer, size, "[THREAD_%d][PACKET_%d] %s %ld", 
                            thread_id, packet_num, 
                            test_strings[packet_num % 5], 
                            time(NULL));
    
    // Fill remaining with random data
    if (base_len < size - 1) {
        for (int i = base_len; i < size - 1; i++) {
            buffer[i] = (rand() % 94) + 33;  // Printable ASCII
        }
        buffer[size - 1] = '\0';
    }
}

// ========================= THREAD WORKER =========================
void *thread_worker(void *arg) {
    ThreadData *data = (ThreadData *)arg;
    unsigned char payload[MAX_PAYLOAD];
    int udp_sock = -1;
    int tcp_sock = -1;
    int packet_num = 0;
    struct sockaddr_in server_addr;
    struct timespec start_time, current_time;
    int elapsed = 0;
    
    // UDP socket for faster sending
    udp_sock = create_udp_socket();
    if (udp_sock < 0) {
        printf("[THREAD %d] UDP socket creation failed\n", data->thread_id);
        pthread_exit(NULL);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(data->target_port);
    server_addr.sin_addr.s_addr = inet_addr(data->target_ip);
    
    // Try TCP connection (optional)
    tcp_sock = create_tcp_socket(data->target_ip, data->target_port);
    if (tcp_sock > 0) {
        close(tcp_sock);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    
    while (*data->running && elapsed < data->duration) {
        // Generate payload
        generate_payload(payload, sizeof(payload), data->thread_id, packet_num);
        int payload_len = strlen((char *)payload);
        
        // Send UDP packet
        int sent = sendto(udp_sock, payload, payload_len, 0,
                         (struct sockaddr *)&server_addr, sizeof(server_addr));
        
        if (sent > 0) {
            pthread_mutex_lock(data->mutex);
            (*data->packet_count)++;
            pthread_mutex_unlock(data->mutex);
            packet_num++;
        }
        
        // Calculate elapsed time
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        elapsed = current_time.tv_sec - start_time.tv_sec;
        
        // Small delay to control rate
        usleep(100);
    }
    
    if (udp_sock > 0) close(udp_sock);
    
    printf("[THREAD %d] Completed: %d packets sent\n", data->thread_id, packet_num);
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
    ThreadData *threads_data = (ThreadData *)arg;
    time_t start_time = time(NULL);
    unsigned long long last_count = 0;
    
    while (global_running) {
        sleep(1);
        
        unsigned long long current_count = total_packets;
        unsigned long long rate = current_count - last_count;
        last_count = current_count;
        
        time_t now = time(NULL);
        int elapsed = now - start_time;
        
        // Clear line and print stats
        printf("\r\033[K");  // Clear line
        printf("\033[32m[STATS]\033[0m Packets: %llu | Rate: %llu pps | Time: %ds", 
               current_count, rate, elapsed);
        fflush(stdout);
        
        // Check if duration exceeded (if using timeout mode)
        if (elapsed >= 3600) {
            global_running = 0;
        }
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
    
    // ====== ARGUMENT CHECK ======
    if (argc != 5) {
        print_usage(argv[0]);
        return 1;
    }
    
    target_ip = argv[1];
    target_port = atoi(argv[2]);
    duration = atoi(argv[3]);
    thread_count = atoi(argv[4]);
    
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
    printf("║         NETWORK PAYLOAD SENDER v1.0                    ║\n");
    printf("║         Network Testing & Performance Tool             ║\n");
    printf("╚══════════════════════════════════════════════════════════╝\n");
    printf("\033[0m\n");
    printf("\033[33m[+] Target: \033[0m%s:%d\n", target_ip, target_port);
    printf("\033[33m[+] Duration: \033[0m%d seconds\n", duration);
    printf("\033[33m[+] Threads: \033[0m%d\n", thread_count);
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
    printf("\033[32m[+] Average Rate: \033[0m%.2f pps\n", 
           (float)total_packets / duration);
    printf("\033[32m[+] Duration: \033[0m%d seconds\n", duration);
    printf("\033[32m[+] Threads Used: \033[0m%d\n", thread_count);
    printf("\n\033[33m[✓] Test completed successfully!\033[0m\n\n");
    
    return 0;
}
