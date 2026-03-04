/**
 * main.c - LwM2M Client using Anjay, connecting to Leshan server
 *
 * Usage:
 *   ./lwm2m_gpio_client -s <leshan_ip> [-p <port>] [-n <endpoint_name>] [-g <gpio_pin>]
 */

#include <anjay/anjay.h>
#include <anjay/security.h>
#include <anjay/server.h>
#include <avsystem/commons/avs_log.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <poll.h>

#include "gpio_object.h"

static volatile int g_running = 1;

static void signal_handler(int signo) {
    (void)signo;
    g_running = 0;
}

/* ------------------------------------------------------------------ */
/*  Setup Security & Server objects to point at Leshan                 */
/* ------------------------------------------------------------------ */

static int setup_security_object(anjay_t *anjay, const char *server_uri) {
    const anjay_security_instance_t security_instance = {
        .ssid = 1,
        .server_uri = server_uri,
        .security_mode = ANJAY_SECURITY_NOSEC,
    };

    if (anjay_security_object_install(anjay)) {
        avs_log(lwm2m_main, ERROR, "Failed to install Security object");
        return -1;
    }

    anjay_iid_t iid = ANJAY_ID_INVALID;
    if (anjay_security_object_add_instance(anjay, &security_instance, &iid)) {
        avs_log(lwm2m_main, ERROR, "Failed to add Security instance");
        return -1;
    }
    return 0;
}

static int setup_server_object(anjay_t *anjay) {
    const anjay_server_instance_t server_instance = {
        .ssid            = 1,
        .lifetime        = 60,
        .default_min_period = -1,
        .default_max_period = -1,
        .binding         = "U",
    };

    if (anjay_server_object_install(anjay)) {
        avs_log(lwm2m_main, ERROR, "Failed to install Server object");
        return -1;
    }

    anjay_iid_t iid = ANJAY_ID_INVALID;
    if (anjay_server_object_add_instance(anjay, &server_instance, &iid)) {
        avs_log(lwm2m_main, ERROR, "Failed to add Server instance");
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Usage / argument parsing                                           */
/* ------------------------------------------------------------------ */

static void print_usage(const char *progname) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  -s <ip>     Leshan server IP (default: localhost)\n"
        "  -p <port>   Leshan CoAP port  (default: 5683)\n"
        "  -n <name>   Endpoint name      (default: rpi-gpio-client)\n"
        "  -g <pin>    BCM GPIO pin       (default: 17)\n"
        "  -h          Show this help\n",
        progname);
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[]) {
    const char *server_ip   = "localhost";
    int         server_port = 5683;
    const char *ep_name     = "rpi-gpio-client";
    int         gpio_pin    = 17;

    int opt;
    while ((opt = getopt(argc, argv, "s:p:n:g:h")) != -1) {
        switch (opt) {
        case 's': server_ip   = optarg;        break;
        case 'p': server_port = atoi(optarg);  break;
        case 'n': ep_name     = optarg;        break;
        case 'g': gpio_pin    = atoi(optarg);  break;
        case 'h':
        default:
            print_usage(argv[0]);
            return (opt == 'h') ? 0 : 1;
        }
    }

    char server_uri[128];
    snprintf(server_uri, sizeof(server_uri), "coap://%s:%d", server_ip, server_port);

    avs_log(lwm2m_main, INFO, "LwM2M GPIO Client starting");
    avs_log(lwm2m_main, INFO, "  Server:   %s", server_uri);
    avs_log(lwm2m_main, INFO, "  Endpoint: %s", ep_name);
    avs_log(lwm2m_main, INFO, "  GPIO pin: %d (BCM)", gpio_pin);

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    const anjay_configuration_t config = {
        .endpoint_name = ep_name,
        .in_buffer_size  = 4096,
        .out_buffer_size = 4096,
        .msg_cache_size  = 4096,
    };

    anjay_t *anjay = anjay_new(&config);
    if (!anjay) {
        avs_log(lwm2m_main, ERROR, "Failed to create Anjay instance");
        return 1;
    }

    if (setup_security_object(anjay, server_uri) ||
        setup_server_object(anjay)) {
        anjay_delete(anjay);
        return 1;
    }

    if (gpio_object_install(anjay)) {
        avs_log(lwm2m_main, ERROR, "Failed to install GPIO object");
        anjay_delete(anjay);
        return 1;
    }

    avs_log(lwm2m_main, INFO, "Client registered – entering event loop");

    /* ---- Main event loop ---- */
    while (g_running) {
        AVS_LIST(avs_net_socket_t *const) sockets = anjay_get_sockets(anjay);

        struct pollfd pollfds[8];
        int num_fds = 0;
        AVS_LIST(avs_net_socket_t *const) sock;
        AVS_LIST_FOREACH(sock, sockets) {
            if (num_fds >= 8) break;
            const void *system_socket = avs_net_socket_get_system(*sock);
            if (system_socket) {
                pollfds[num_fds].fd      = *(const int *)system_socket;
                pollfds[num_fds].events  = POLLIN;
                pollfds[num_fds].revents = 0;
                num_fds++;
            }
        }

        int timeout_ms = anjay_sched_calculate_wait_time_ms(anjay, 100);

        poll(pollfds, num_fds, timeout_ms);

        int idx = 0;
        AVS_LIST_FOREACH(sock, sockets) {
            if (idx < num_fds && (pollfds[idx].revents & POLLIN)) {
                anjay_serve(anjay, *sock);
            }
            idx++;
        }

        anjay_sched_run(anjay);

        gpio_object_update(anjay);
    }

    avs_log(lwm2m_main, INFO, "Shutting down...");
    gpio_object_cleanup();
    anjay_delete(anjay);

    return 0;
}
