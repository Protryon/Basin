//
// Created by p on 4/14/19.
//

#ifndef BASIN_CONNECTION_H
#define BASIN_CONNECTION_H

#include <avuna/netmgr.h>
#include <avuna/pmem.h>

struct connection {
    struct mempool* pool;
    struct netmgr_connection* managed_conn;
    union {
        struct sockaddr_in in;
        struct sockaddr_in6 in6;
    } addr;
    socklen_t addrlen;
    char* host_ip;
    uint16_t host_port;
    struct player* player;
    int disconnect;
    uint32_t protocolVersion;
    uint32_t verifyToken;
    char* online_username;
    uint8_t shared_secret[16];
    EVP_CIPHER_CTX* aes_ctx_enc;
    EVP_CIPHER_CTX* aes_ctx_dec;
    uint32_t protocol_state;

};

#endif //BASIN_CONNECTION_H