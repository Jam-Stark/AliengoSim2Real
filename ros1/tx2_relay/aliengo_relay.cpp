/**
 * aliengo_relay.cpp — TX2 UDP relay for Aliengo v3.0.0
 *
 * Runs on the TX2 (ARM64). Bridges between:
 *   - External PC (Docker) via raw UDP on RELAY_PORT
 *   - Aliengo motion controller via v3.0.0 SDK on 192.168.123.10:8007
 *
 * Protocol:
 *   External PC → TX2: 730 bytes (sizeof LowCmd struct)
 *   TX2 → External PC: 891 bytes (sizeof LowState struct)
 *
 * Build on TX2:
 *   cd /home/unitree/unitree_legged_sdk
 *   g++ -I include -L lib -O2 -o aliengo_relay aliengo_relay.cpp \
 *       -lunitree_legged_sdk -lpthread -llcm
 *
 * Run on TX2:
 *   LD_LIBRARY_PATH=lib sudo ./aliengo_relay
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <pthread.h>
#include "unitree_legged_sdk/unitree_legged_sdk.h"

using namespace UNITREE_LEGGED_SDK;

// ============================================================
// Configuration
// ============================================================
constexpr int RELAY_PORT = 9000;     // Port for external PC
constexpr int SEND_RECV_HZ = 500;   // SDK send/recv frequency

// ============================================================
// Global state
// ============================================================
static UDP *sdk_udp = nullptr;
static LowCmd g_cmd = {0};
static LowState g_state = {0};
static pthread_mutex_t cmd_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t state_mutex = PTHREAD_MUTEX_INITIALIZER;

// External PC address (learned from first received packet)
static struct sockaddr_in ext_addr;
static bool ext_connected = false;
static int relay_sock = -1;

// ============================================================
// SDK threads (send/recv to motion controller at 500Hz)
// ============================================================

void sdkSendCallback() {
    pthread_mutex_lock(&cmd_mutex);
    sdk_udp->SetSend(g_cmd);
    pthread_mutex_unlock(&cmd_mutex);
    sdk_udp->Send();
}

void sdkRecvCallback() {
    sdk_udp->Recv();
    pthread_mutex_lock(&state_mutex);
    sdk_udp->GetRecv(g_state);
    pthread_mutex_unlock(&state_mutex);
}

// ============================================================
// Relay thread: forward between external PC and SDK
// ============================================================

void *relayThread(void *) {
    unsigned char cmd_buf[sizeof(LowCmd)];
    unsigned char state_buf[sizeof(LowState)];
    struct sockaddr_in sender;
    socklen_t sender_len = sizeof(sender);

    printf("[Relay] Listening on port %d for external PC...\n", RELAY_PORT);
    printf("[Relay] sizeof(LowCmd)=%zu, sizeof(LowState)=%zu\n",
           sizeof(LowCmd), sizeof(LowState));

    while (true) {
        // Non-blocking receive from external PC
        ssize_t n = recvfrom(relay_sock, cmd_buf, sizeof(cmd_buf), 0,
                             (struct sockaddr *)&sender, &sender_len);

        if (n == (ssize_t)sizeof(LowCmd)) {
            // Got a valid LowCmd from external PC
            if (!ext_connected) {
                ext_addr = sender;
                ext_connected = true;
                printf("[Relay] External PC connected from %s:%d\n",
                       inet_ntoa(sender.sin_addr),
                       ntohs(sender.sin_port));
            }

            // Copy into global cmd
            pthread_mutex_lock(&cmd_mutex);
            memcpy(&g_cmd, cmd_buf, sizeof(LowCmd));
            pthread_mutex_unlock(&cmd_mutex);
        }

        // Send latest state to external PC
        if (ext_connected) {
            pthread_mutex_lock(&state_mutex);
            memcpy(state_buf, &g_state, sizeof(LowState));
            pthread_mutex_unlock(&state_mutex);

            sendto(relay_sock, state_buf, sizeof(LowState), 0,
                   (struct sockaddr *)&ext_addr, sizeof(ext_addr));
        }

        // ~500 Hz
        usleep(2000);
    }

    return nullptr;
}

// ============================================================
// Main
// ============================================================

int main(int argc, char **argv) {
    printf("============================================\n");
    printf("  Aliengo v3.0.0 TX2 UDP Relay\n");
    printf("============================================\n");

    int relay_port = RELAY_PORT;
    if (argc > 1) relay_port = atoi(argv[1]);

    // 1. Initialize SDK UDP (to motion controller)
    sdk_udp = new UDP(LOWLEVEL);
    sdk_udp->InitCmdData(g_cmd);
    printf("[SDK] Initialized. Target: 192.168.123.10:8007\n");

    // 2. Create relay socket
    relay_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (relay_sock < 0) {
        perror("socket");
        return 1;
    }

    // Set recv timeout (10ms)
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 10000;
    setsockopt(relay_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(relay_port);

    if (bind(relay_sock, (struct sockaddr *)&local_addr,
             sizeof(local_addr)) < 0) {
        perror("bind");
        return 1;
    }
    printf("[Relay] Bound to port %d\n", relay_port);

    // 3. Start SDK send/recv threads
    LoopFunc loop_send("sdk_send", 1.0 / SEND_RECV_HZ, 3,
                       boost::bind(sdkSendCallback));
    LoopFunc loop_recv("sdk_recv", 1.0 / SEND_RECV_HZ, 3,
                       boost::bind(sdkRecvCallback));
    loop_send.start();
    loop_recv.start();
    printf("[SDK] Send/Recv threads started at %d Hz\n", SEND_RECV_HZ);

    // 4. Start relay thread
    pthread_t relay_tid;
    pthread_create(&relay_tid, nullptr, relayThread, nullptr);

    printf("[Relay] Running. Press Ctrl+C to stop.\n\n");

    // Main thread: periodic status print
    while (true) {
        sleep(5);
        pthread_mutex_lock(&state_mutex);
        float qw = g_state.imu.quaternion[0];
        float m0q = g_state.motorState[0].q;
        pthread_mutex_unlock(&state_mutex);
        printf("[Status] IMU quat_w=%.4f  motor[0].q=%.4f  ext_connected=%d\n",
               qw, m0q, ext_connected ? 1 : 0);
    }

    delete sdk_udp;
    return 0;
}
