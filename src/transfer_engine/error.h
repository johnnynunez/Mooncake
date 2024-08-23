// error.h
// Copyright (C) 2024 Feng Ren

#ifndef ERROR_H
#define ERROR_H

#define ERR_INVALID_ARGUMENT        (-1)
#define ERR_TOO_MANY_REQUESTS       (-2)
#define ERR_ADDRESS_NOT_REGISTERED  (-3)
#define ERR_BATCH_BUSY              (-4)
#define ERR_DEVICE_NOT_FOUND        (-6)
#define ERR_ADDRESS_OVERLAPPED      (-7)

#define ERR_DNS_FAIL            (-101)
#define ERR_SOCKET_FAIL         (-102)
#define ERR_MALFORMED_JSON      (-103)
#define ERR_REJECT_HANDSHAKE    (-104)
#define ERR_MALFORMED_RESPONSE  (-105)

#define ERR_METADATA            (-200)
#define ERR_ENDPOINT            (-201)
#define ERR_CONTEXT             (-202)

#define ERR_NUMA                (-300)
#define ERR_CLOCK               (-301)
#define ERR_OUT_OF_MEMORY       (-302)

#endif // ERROR_H