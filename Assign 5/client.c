#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MSG_LEN 1024

// Message types
#define TYPE_DATA 1
#define TYPE_ACK 2
#define TYPE_TERM 3

// Message structure
typedef struct {
    int type;
    char content[MSG_LEN];
} Message;

// Base64 encoding table
static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Function prototypes
char *base64_encode(const char *input);
void send_message(int sock, const char *input, struct sockaddr_in *server_addr, int is_tcp);
void receive_ack(int sock, struct sockaddr_in *server_addr, int is_tcp);

// Base64 encoding function
char *base64_encode(const char *input) {
    size_t input_len = strlen(input);
    size_t output_len = 4 * ((input_len + 2) / 3);
    
    char *encoded = malloc(output_len + 1);
    if (!encoded) return NULL;
    
    for (size_t i = 0, j = 0; i < input_len;) {
        uint32_t octet_a = i < input_len ? (unsigned char)input[i++] : 0;
        uint32_t octet_b = i < input_len ? (unsigned char)input[i++] : 0;
        uint32_t octet_c = i < input_len ? (unsigned char)input[i++] : 0;
        
        uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;
        
        encoded[j++] = base64_chars[(triple >> 3 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 2 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 1 * 6) & 0x3F];
        encoded[j++] = base64_chars[(triple >> 0 * 6) & 0x3F];
    }
    
    for (size_t i = 0; i < (3 - input_len % 3) % 3; i++)
        encoded[output_len - 1 - i] = '=';
    
    encoded[output_len] = '\0';
    return encoded;
}

void send_message(int sock, const char *input, struct sockaddr_in *server_addr, int is_tcp) {
    Message msg;
    msg.type = TYPE_DATA;
    char *encoded = base64_encode(input);
    
    // Print the Base64-encoded message
    printf("Original message: %s\n", input);
    printf("Base64-encoded message: %s\n", encoded);
    
    strncpy(msg.content, encoded, MSG_LEN);
    free(encoded);
    
    if (is_tcp) {
        send(sock, &msg, sizeof(Message), 0);
    } else {
        sendto(sock, &msg, sizeof(Message), 0, (struct sockaddr *)server_addr, sizeof(*server_addr));
    }
}


// Receive acknowledgment function
void receive_ack(int sock, struct sockaddr_in *server_addr, int is_tcp) {
    Message ack_msg;
    if (is_tcp) {
        recv(sock, &ack_msg, sizeof(Message), 0);
    } else {
        socklen_t addr_len = sizeof(*server_addr);
        recvfrom(sock, &ack_msg, sizeof(Message), 0, (struct sockaddr *)server_addr, &addr_len);
    }
    
    if (ack_msg.type == TYPE_ACK) {
        printf("Server acknowledgment: %s\n", ack_msg.content);
    }
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <server_ip> <server_port> <tcp/udp>\n", argv[0]);
        exit(1);
    }
    
    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    int is_tcp = strcmp(argv[3], "tcp") == 0;
    
    int sock;
    struct sockaddr_in server_addr;
    
    // Create socket
    if (is_tcp) {
        sock = socket(AF_INET, SOCK_STREAM, 0);
    } else {
        sock = socket(AF_INET, SOCK_DGRAM, 0);
    }
    
    if (sock < 0) {
        perror("Socket creation failed");
        exit(1);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    // Connect if using TCP
    if (is_tcp) {
        if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection failed");
            exit(1);
        }
    }
    
    printf("Connected to server. Start typing messages (type 'exit' to quit):\n");
    
    char input[MSG_LEN];
    while (1) {
        fgets(input, MSG_LEN, stdin);
        input[strcspn(input, "\n")] = 0;  // Remove newline
        
        if (strcmp(input, "exit") == 0) {
            // Send termination message
            Message term_msg;
            term_msg.type = TYPE_TERM;
            strcpy(term_msg.content, "Terminating connection");
            
            if (is_tcp) {
                send(sock, &term_msg, sizeof(Message), 0);
            } else {
                sendto(sock, &term_msg, sizeof(Message), 0, (struct sockaddr *)&server_addr, sizeof(server_addr));
            }
            break;
        }
        
        send_message(sock, input, &server_addr, is_tcp);
        receive_ack(sock, &server_addr, is_tcp);
    }
    
    close(sock);
    return 0;
}

