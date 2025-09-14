#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MSG_LEN 1024
#define MAX_CLIENTS 10

// Message types
#define TYPE_DATA 1
#define TYPE_ACK 2
#define TYPE_TERM 3

// Message structure
typedef struct {
    int type;
    char content[MSG_LEN];
} Message;

// Thread argument structure
typedef struct {
    int client_socket;
    struct sockaddr_in client_addr;
    int client_id;  // Added client ID
} ThreadArgs;

// Global client ID counter with mutex for thread safety
static int next_client_id = 1;
pthread_mutex_t client_id_mutex = PTHREAD_MUTEX_INITIALIZER;

// Base64 encoding/decoding tables
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Function prototypes
void *handle_tcp_client(void *arg);
void handle_udp_message(int udp_socket);
char *base64_decode(const char *input);
void send_ack(int socket, struct sockaddr_in *client_addr, int is_tcp);

// Base64 decoding function
char *base64_decode(const char *input) {
    size_t input_len = strlen(input);
    size_t output_len = input_len / 4 * 3;
    
    // Adjust for padding
    if (input[input_len - 1] == '=') output_len--;
    if (input[input_len - 2] == '=') output_len--;
    
    char *output = malloc(output_len + 1);
    if (!output) return NULL;
    
    // Decoding logic
    size_t i, j = 0;
    unsigned char a, b, c, d;
    unsigned char triple[3];
    
    for (i = 0; i < input_len; i += 4) {
        // Get values for each base64 character
        a = strchr(base64_chars, input[i]) - base64_chars;
        b = strchr(base64_chars, input[i+1]) - base64_chars;
        c = (input[i+2] == '=') ? 0 : strchr(base64_chars, input[i+2]) - base64_chars;
        d = (input[i+3] == '=') ? 0 : strchr(base64_chars, input[i+3]) - base64_chars;
        
        // Reconstruct the original bytes
        triple[0] = (a << 2) | (b >> 4);
        triple[1] = (b << 4) | (c >> 2);
        triple[2] = (c << 6) | d;
        
        // Add bytes to output
        for (j = 0; j < 3 && (i*3/4+j) < output_len; j++) {
            output[i*3/4+j] = triple[j];
        }
    }
    
    output[output_len] = '\0';
    return output;
}

// TCP client handler (runs in a separate thread)
void *handle_tcp_client(void *arg) {
    ThreadArgs *args = (ThreadArgs *)arg;
    int client_socket = args->client_socket;
    struct sockaddr_in client_addr = args->client_addr;
    int client_id = args->client_id;  // Get client ID
    free(args);
    
    printf("TCP client #%d connected: %s:%d\n", 
           client_id, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
    
    Message msg;
    while (1) {
        // Receive message from client
        ssize_t bytes_received = recv(client_socket, &msg, sizeof(Message), 0);
        if (bytes_received <= 0) {
            printf("TCP client #%d disconnected\n", client_id);
            break;
        }
        
        // Handle message based on type
        if (msg.type == TYPE_DATA) {
            // Print the encoded message
            printf("Received Base64-encoded message from client #%d: %s\n", client_id, msg.content);
            
            // Decode Base64 message
            char *decoded = base64_decode(msg.content);
            printf("Decoded message from client #%d: %s\n", client_id, decoded);
            free(decoded);
            
            // Send acknowledgment
            Message ack_msg;
            ack_msg.type = TYPE_ACK;
            strcpy(ack_msg.content, "Message received successfully");
            send(client_socket, &ack_msg, sizeof(Message), 0);
        }
        else if (msg.type == TYPE_TERM) {
            printf("TCP client #%d requested termination\n", client_id);
            break;
        }
    }
    
    // Close client socket
    close(client_socket);
    pthread_exit(NULL);
}

// Handle UDP messages
void handle_udp_message(int udp_socket) {
    Message msg;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    // Receive message from client
    ssize_t bytes_received = recvfrom(udp_socket, &msg, sizeof(Message), 0,
                                     (struct sockaddr *)&client_addr, &addr_len);
    
    if (bytes_received > 0) {
        // For UDP, we use IP:port as identifier since there's no persistent connection
        printf("UDP message from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));
        
        // Handle message based on type
        if (msg.type == TYPE_DATA) {
            // Print the encoded message
            printf("Received Base64-encoded message from UDP client %s:%d: %s\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), msg.content);
            
            // Decode Base64 message
            char *decoded = base64_decode(msg.content);
            printf("Decoded message from UDP client %s:%d: %s\n", 
                   inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), decoded);
            free(decoded);
            
            // Send acknowledgment
            Message ack_msg;
            ack_msg.type = TYPE_ACK;
            strcpy(ack_msg.content, "Message received successfully");
            sendto(udp_socket, &ack_msg, sizeof(Message), 0,
                  (struct sockaddr *)&client_addr, addr_len);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    
    // Initialize mutex
    pthread_mutex_init(&client_id_mutex, NULL);
    
    // Create TCP socket
    int tcp_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (tcp_socket < 0) {
        perror("Failed to create TCP socket");
        return 1;
    }
    
    // Create UDP socket
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0) {
        perror("Failed to create UDP socket");
        close(tcp_socket);
        return 1;
    }
    
    // Set socket options to reuse address
    int opt = 1;
    setsockopt(tcp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Configure server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind TCP socket
    if (bind(tcp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind TCP socket");
        close(tcp_socket);
        close(udp_socket);
        return 1;
    }
    
    // Bind UDP socket
    if (bind(udp_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Failed to bind UDP socket");
        close(tcp_socket);
        close(udp_socket);
        return 1;
    }
    
    // Listen for TCP connections
    if (listen(tcp_socket, MAX_CLIENTS) < 0) {
        perror("Failed to listen on TCP socket");
        close(tcp_socket);
        close(udp_socket);
        return 1;
    }
    
    printf("Server started on port %d\n", port);
    printf("Waiting for connections...\n");
    
    // Set up file descriptor set for select()
    fd_set read_fds;
    int max_fd = (tcp_socket > udp_socket) ? tcp_socket : udp_socket;
    
    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(tcp_socket, &read_fds);
        FD_SET(udp_socket, &read_fds);
        FD_SET(STDIN_FILENO, &read_fds);  // Add stdin to check for server termination command
        
        // Update max_fd to include stdin
        max_fd = (STDIN_FILENO > max_fd) ? STDIN_FILENO : max_fd;
        
        // Wait for activity on any socket
        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("Select failed");
            break;
        }
        
        // Check for server termination command
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            char input[10];
            if (fgets(input, sizeof(input), stdin) != NULL) {
                if (strcmp(input, "quit\n") == 0) {
                    printf("Server shutting down...\n");
                    break;  // Exit the main loop
                }
            }
        }
        
        // Check for TCP connection
        if (FD_ISSET(tcp_socket, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t addr_len = sizeof(client_addr);
            int client_socket = accept(tcp_socket, (struct sockaddr *)&client_addr, &addr_len);
            
            if (client_socket < 0) {
                perror("Failed to accept TCP connection");
                continue;
            }
            
            // Get a unique client ID (thread-safe)
            pthread_mutex_lock(&client_id_mutex);
            int client_id = next_client_id++;
            pthread_mutex_unlock(&client_id_mutex);
            
            // Create thread to handle client
            pthread_t thread_id;
            ThreadArgs *args = malloc(sizeof(ThreadArgs));
            args->client_socket = client_socket;
            args->client_addr = client_addr;
            args->client_id = client_id;  // Pass client ID to thread
            
            if (pthread_create(&thread_id, NULL, handle_tcp_client, args) != 0) {
                perror("Failed to create thread");
                free(args);
                close(client_socket);
            } else {
                pthread_detach(thread_id);
            }
        }
        
        // Check for UDP message
        if (FD_ISSET(udp_socket, &read_fds)) {
            handle_udp_message(udp_socket);
        }
    }
    
    // Clean up
    pthread_mutex_destroy(&client_id_mutex);
    close(tcp_socket);
    close(udp_socket);
    
    return 0;
}

