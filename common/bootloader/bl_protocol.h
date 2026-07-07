#ifndef BL_PROTOCOL_H
#define BL_PROTOCOL_H

#include <stdint.h>

#define REPLY_WAITING              ((uint8_t)1u)
#define REPLY_ACK                  ((uint8_t)2u)
#define ERR_SIZE                   ((uint8_t)3u)
#define ERR_RECEIVE                ((uint8_t)4u)
#define ERR_CRC32                  ((uint8_t)5u)
#define ERR_WAIT_WRITE_PAGE        ((uint8_t)6u)
#define ERR_WAIT_ERASE_PAGE        ((uint8_t)7u)

#define BL_UPDATE_WAIT_TIMEOUT_MS           ((uint32_t)500u)

#endif /* BL_PROTOCOL_H */
