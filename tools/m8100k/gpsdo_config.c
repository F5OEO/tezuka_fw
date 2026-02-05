/*
 * GPSDO 100kHz Extension - Linux Version (GPS Configuration Only)
 * Converted from Arduino code by F1CJN/F1TE
 * 
 * This version only configures the UBLOX GPS TimePulse output
 * 
 * Compile: gcc -o gpsdo_config gpsdo_config.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

// Configuration
#define GPS_DEVICE "/dev/ttyACM0"
#define GPS_BAUD B9600
#define FREQUENCY 100000  // 100 KHz TimePulse

// Global variables
int gps_fd = -1;

// Function prototypes
int setup_serial(const char *device, speed_t baud);
int configure_ublox(int fd, uint32_t frequency);
void calc_checksum(uint8_t *payload, size_t size);
int send_ubx(int fd, uint8_t *msg, size_t len);
int get_ubx_ack(int fd, uint8_t *msg_id);

// Serial port setup
int setup_serial(const char *device, speed_t baud) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("Unable to open serial port");
        return -1;
    }

    struct termios options;
    tcgetattr(fd, &options);
    
    cfsetispeed(&options, baud);
    cfsetospeed(&options, baud);
    
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag &= ~CRTSCTS;
    
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;
    
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 10;
    
    tcsetattr(fd, TCSANOW, &options);
    tcflush(fd, TCIOFLUSH);
    
    return fd;
}

// Calculate UBX checksum
void calc_checksum(uint8_t *payload, size_t size) {
    uint8_t ck_a = 0, ck_b = 0;
    
    for (size_t i = 0; i < size; i++) {
        ck_a += payload[i];
        ck_b += ck_a;
    }
    
    payload[size] = ck_a;
    payload[size + 1] = ck_b;
}

// Send UBX message
int send_ubx(int fd, uint8_t *msg, size_t len) {
    ssize_t written = write(fd, msg, len);
    if (written != (ssize_t)len) {
        perror("Error writing to GPS");
        return -1;
    }
    tcdrain(fd);
    return 0;
}

// Get UBX ACK
int get_ubx_ack(int fd, uint8_t *msg_id) {
    uint8_t ack_packet[10] = {0xB5, 0x62, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint8_t incoming;
    int i = 0;
    time_t start = time(NULL);
    
    while (1) {
        if (read(fd, &incoming, 1) > 0) {
            if (incoming == ack_packet[i]) {
                i++;
            } else if (i > 2) {
                ack_packet[i] = incoming;
                i++;
            }
        }
        
        if (i > 9) break;
        
        if (difftime(time(NULL), start) > 1.5) {
            printf("ACK Timeout\n");
            return 5;
        }
        
        if (i == 4 && ack_packet[3] == 0x00) {
            printf("NAK Received\n");
            return 1;
        }
    }
    
    uint8_t ck_a = 0, ck_b = 0;
    for (i = 2; i < 8; i++) {
        ck_a += ack_packet[i];
        ck_b += ck_a;
    }
    
    if (msg_id[0] == ack_packet[6] && msg_id[1] == ack_packet[7] && 
        ck_a == ack_packet[8] && ck_b == ack_packet[9]) {
        printf("Success! ACK Received\n");
        return 10;
    }
    
    printf("ACK Checksum Failure\n");
    return 1;
}

// Configure UBLOX GPS TimePulse
int configure_ublox(int fd, uint32_t frequency) {
    uint8_t buf[4];
    int success = 0;
    int attempts = 0;
    
    printf("\nConfiguring UBLOX GPS TimePulse for %u Hz\n", frequency);
    printf("Note: Only frequencies with even integer divisors of 48 MHz have low jitter\n");
    printf("Examples: 8 MHz (div 6), 10 KHz (div 4800) - Good\n");
    printf("          10 MHz (div 4.8) - Not recommended\n\n");
    
    // Prepare frequency bytes (little endian)
    buf[0] = (frequency & 0x000000FF);
    buf[1] = (frequency & 0x0000FF00) >> 8;
    buf[2] = (frequency & 0x00FF0000) >> 16;
    buf[3] = (frequency & 0xFF000000) >> 24;
    
    // UBX-CFG-TP5 message (TimePulse configuration)
    uint8_t set_timepulse[] = {
        0xB5, 0x62,           // Header
        0x06, 0x31,           // Class ID and Message ID (CFG-TP5)
        0x20, 0x00,           // Payload length (32 bytes)
        0x00,                 // tpIdx (TimePulse selection, 0 = TIMEPULSE)
        0x01,                 // version
        0x00, 0x00,           // reserved1
        0x32, 0x00,           // antCableDelay (50 ns)
        0x00, 0x00,           // rfGroupDelay
        buf[0], buf[1], buf[2], buf[3],  // freqPeriod (frequency when locked)
        buf[0], buf[1], buf[2], buf[3],  // freqPeriodLock (frequency when locked)
        0x00, 0x00, 0x00, 0x80,          // pulseLenRatio (50% duty cycle = 0x80000000)
        0x00, 0x00, 0x00, 0x80,          // pulseLenRatioLock (50% duty cycle)
        0x00, 0x00, 0x00, 0x00,          // userConfigDelay
        0xEF, 0x00, 0x00, 0x00,          // flags (active, lock to GPS, isFreq, aligned to TOW)
        0x00, 0x00                        // Checksum (will be calculated)
    };
    
    calc_checksum(&set_timepulse[2], sizeof(set_timepulse) - 4);
    
    printf("Waiting 1 second before configuration...\n");
    sleep(1);
    
    while (success < 3 && attempts < 10) {
        attempts++;
        printf("\n[Attempt %d] Sending TimePulse configuration...\n", attempts);
        
        if (send_ubx(fd, set_timepulse, sizeof(set_timepulse)) < 0) {
            printf("Failed to send UBX message\n");
            continue;
        }
        
        int ack = get_ubx_ack(fd, &set_timepulse[2]);
        
        if (ack == 10) {
            success++;
            printf("Configuration acknowledged (%d/3)\n", success);
        } else if (ack == 5) {
            printf("Timeout waiting for ACK, retrying...\n");
        } else if (ack == 1) {
            printf("NAK received or checksum failure, retrying...\n");
        }
        
        if (success < 3) {
            usleep(500000); // Wait 500ms before retry
        }
    }
    
    if (success >= 3) {
        printf("\n✓ TimePulse configured successfully!\n");
        printf("  Frequency: %u Hz\n", frequency);
        printf("  Duty Cycle: 50%%\n");
        printf("  Output: Active when GPS locked\n");
        return 0;
    } else {
        printf("\n✗ Failed to configure TimePulse after %d attempts\n", attempts);
        return -1;
    }
}

// Main function
int main(int argc, char *argv[]) {
    uint32_t frequency = FREQUENCY;
    const char *device = GPS_DEVICE;
    
    printf("========================================\n");
    printf("  GPSDO GPS Configuration Tool\n");
    printf("  Based on Arduino code by F1CJN/F1TE\n");
    printf("========================================\n");
    
    // Parse command line arguments
    if (argc > 1) {
        frequency = atoi(argv[1]);
        printf("\nUsing custom frequency: %u Hz\n", frequency);
    }
    
    if (argc > 2) {
        device = argv[2];
        printf("Using custom device: %s\n", device);
    }
    
    // Setup serial port for GPS
    printf("\nOpening GPS device: %s\n", device);
    gps_fd = setup_serial(device, GPS_BAUD);
    if (gps_fd < 0) {
        fprintf(stderr, "Failed to open GPS device\n");
        return 1;
    }
    
    printf("GPS device opened successfully (fd=%d)\n", gps_fd);
    
    // Configure UBLOX
    int result = configure_ublox(gps_fd, frequency);
    
    // Cleanup
    close(gps_fd);
    
    if (result == 0) {
        printf("\n========================================\n");
        printf("Configuration completed successfully!\n");
        printf("The GPS TimePulse output is now active.\n");
        printf("========================================\n");
        return 0;
    } else {
        printf("\n========================================\n");
        printf("Configuration failed!\n");
        printf("Please check your GPS connection.\n");
        printf("========================================\n");
        return 1;
    }
}
