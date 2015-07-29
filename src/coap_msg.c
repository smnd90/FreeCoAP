/*
 * Copyright (c) 2015 Keith Cullen.
 * All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 *  @file coap_msg.c
 *
 *  @brief Source file for the FreeCoAP message parser/formatter library
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include "coap_msg.h"

#define coap_msg_op_list_get_first(list)       ((list)->first)                  /**< Get the first option from an option linked-list */
#define coap_msg_op_list_get_last(list)        ((list)->last)                   /**< Get the last option in an option linked-list */
#define coap_msg_op_list_is_empty(list)        ((list)->first == NULL)          /**< Indicate if an option linked-list is empty */

static int coap_msg_rand_init = 0;                                              /**< Indicates if the random number generator has been initialised */

void coap_msg_gen_rand_str(char *buf, unsigned len)
{
    unsigned i = 0;

    if (!coap_msg_rand_init)
    {
        srand(time(NULL));
        coap_msg_rand_init = 1;
    }
    for (i = 0; i < len; i++)
    {
        buf[i] = rand() & 0x000000ff;
    }
}

/**
 *  @brief Allocate an option structure
 *
 *  @param[in] num Option number
 *  @param[in] len Option length
 *  @param[in] val Pointer to the option value
 *
 *  @returns Pointer to the option structure
 *  @retval NULL Out-of-memory
 */
static coap_msg_op_t *coap_msg_op_new(unsigned num, unsigned len, char *val)
{
    coap_msg_op_t *op = NULL;

    op = (coap_msg_op_t *)malloc(sizeof(coap_msg_op_t));
    if (op == NULL)
    {
        return NULL;
    }
    op->num = num;
    op->len = len;
    op->val = (char *)malloc(len);
    if (op->val == NULL)
    {
        free(op);
        return NULL;
    }
    memcpy(op->val, val, len);
    op->next = NULL;
    return op;
}

/**
 *  @brief Free an option structure that was allocated by coap_msg_op_new
 *
 *  @param[in] op Pointer to the option structure
 */
static void coap_msg_op_delete(coap_msg_op_t *op)
{
    free(op->val);
    free(op);
}

/**
 *  @brief Initialise an option linked-list structure
 *
 *  @param[in,out] list Pointer to an option linked-list structure
 */
static void coap_msg_op_list_create(coap_msg_op_list_t *list)
{
    memset(list, 0, sizeof(coap_msg_op_list_t));
}

/**
 *  @brief Deinitialise an option linked-list structure
 *
 *  @param[in,out] list Pointer to an option linked-list structure
 */
static void coap_msg_op_list_destroy(coap_msg_op_list_t *list)
{
    coap_msg_op_t *prev = NULL;
    coap_msg_op_t *op = NULL;

    op = list->first;
    while (op != NULL)
    {
        prev = op;
        op = op->next;
        coap_msg_op_delete(prev);
    }
    memset(list, 0, sizeof(coap_msg_op_list_t));
}

/**
 *  @brief Allocate an option structure and add it to the end of an option linked-list structure
 *
 *  @param[out] list Pointer to an option linked-list structure
 *  @param[in] num Option number
 *  @param[in] len Option length
 *  @param[in] val Pointer to a buffer containing the option value
 *
 *  @returns Operation status
 *  @retval 0 Success
 *  @retval -ENOMEM Out-of-memory
 */
static int coap_msg_op_list_add_last(coap_msg_op_list_t *list, unsigned num, unsigned len, char *val)
{
    coap_msg_op_t *op = NULL;

    op = coap_msg_op_new(num, len, val);
    if (op == NULL)
    {
        return -ENOMEM;
    }
    if (list->first == NULL)
    {
        list->first = op;
        list->last = op;
    }
    else
    {
        list->last->next = op;
        list->last = op;
    }
    return 0;
}

/**
 *  @brief Allocate an option structure and add it to an option linked-list structure
 *
 *  The option is added to the list at a position determined by the option number.
 *
 *  @param[in] list Pointer to an option linked-list structure
 *  @param[in] num Option number
 *  @param[in] len Option length
 *  @param[in] val Pointer to a buffer containing the option value
 *
 *  @returns Operation status
 *  @retval 0 Success
 *  @retval -ENOMEM Out-of-memory
 */
static int coap_msg_op_list_add(coap_msg_op_list_t *list, unsigned num, unsigned len, char *val)
{
    coap_msg_op_t *prev = NULL;
    coap_msg_op_t *op = NULL;

    op = coap_msg_op_new(num, len, val);
    if (op == NULL)
    {
        return -ENOMEM;
    }
    if (list->first == NULL)
    {
        list->first = op;
        list->last = op;
    }
    else
    {
        prev = list->first;
        while (prev != NULL)
        {
            if (prev->num < op->num)
            {
                op->next = prev->next;
                prev->next = op;
                if (prev == list->last)
                {
                    list->last = op;
                }
                break;
            }
            prev = prev->next;
        }
        if (prev == NULL)
        {
            list->last->next = op;
            list->last = op;
        }
    }
    return 0;
}

void coap_msg_create(coap_msg_t *msg)
{
    memset(msg, 0, sizeof(coap_msg_t));
    msg->ver = COAP_MSG_VER;
    coap_msg_op_list_create(&msg->op_list);
}

void coap_msg_destroy(coap_msg_t *msg)
{
    coap_msg_op_list_destroy(&msg->op_list);
    if (msg->payload != NULL)
    {
        free(msg->payload);
    }
    memset(msg, 0, sizeof(coap_msg_t));
}

void coap_msg_reset(coap_msg_t *msg)
{
    coap_msg_destroy(msg);
    coap_msg_create(msg);
}

/**
 *  @brief Check a message for correctness
 *
 *  The following checks from RFC7252 are performed:
 *
 *  An Empty message has the Code field set to 0.00. The Token Length
 *  field MUST be set to 0 and bytes of data MUST NOT be present after
 *  the Message ID field.  If there are any bytes, they MUST be processed
 *  as a message format error.
 *
 *  The Reset message MUST echo the Message ID of the Confirmable message
 *  and MUST be Empty.
 *
 *  A Non-confirmable message always carries either a request or response
 *  and MUST NOT be Empty.
 *
 *  @param[in] msg Pointer to a message structure
 *  @returns Operation status
 *  @retval 0 Success
 *  @retval -EBADMSG The message is corrupt
 */
static int coap_msg_check(coap_msg_t *msg)
{
    if ((msg->code_class == 0) && (msg->code_detail == 0))
    {
        /* empty message */
        if ((msg->type == COAP_MSG_NON)
         || (msg->token_len != 0)
         || (!coap_msg_op_list_is_empty(&msg->op_list))
         || (msg->payload_len != 0))
        {
            return -EBADMSG;
        }
    }
    else
    {
        /* non-empty message */
        if (msg->type == COAP_MSG_RST)
        {
            return -EBADMSG;
        }
    }
    return 0;
}

int coap_msg_parse_type_msg_id(char *buf, unsigned len, unsigned *type, unsigned *msg_id)
{
    if (len < 4)
    {
        return -EBADMSG;
    }
    *type = (buf[0] >> 4) & 0x03;
    *msg_id = ntohs(*((uint16_t *)(&buf[2])));
    return 0;
}

/**
 *  @brief Parse the header in a message
 *
 *  @param[out] msg Pointer to a message structure
 *  @param[in] buf Pointer to a buffer containing the message
 *  @param[in] len Length of the buffer
 *
 *  @returns Number of bytes parsed or error code
 *  @retval >0 Number of bytes parsed
 *  @retval -EINVAL Invalid argument
 *  @retval -EBADMSG The message is corrupt
 */
static int coap_msg_parse_hdr(coap_msg_t *msg, char *buf, unsigned len)
{
    char *p = buf;

    if (len < 4)
    {
        return -EBADMSG;
    }
    msg->ver = (p[0] >> 6) & 0x03;
    if (msg->ver != COAP_MSG_VER)
    {
        return -EINVAL;
    }
    msg->type = (p[0] >> 4) & 0x03;
    msg->token_len = p[0] & 0x0f;
    if ((msg->token_len > sizeof(msg->token))
     || (msg->token_len > 8))
    {
        return -EBADMSG;
    }
    msg->code_detail = p[1] & 0x1f;
    msg->code_class = (p[1] >> 5) & 0x07;
    if ((msg->code_class != COAP_MSG_REQ)
     && (msg->code_class != COAP_MSG_SUCCESS)
     && (msg->code_class != COAP_MSG_CLIENT_ERR)
     && (msg->code_class != COAP_MSG_SERVER_ERR))
    {
        return -EBADMSG;
    }
    msg->msg_id = ntohs(*((uint16_t *)(&p[2])));
    p += 4;
    len -= 4;
    return p - buf;
}

/**
 *  @brief Parse the token in a message
 *
 *  @param[out] msg Pointer to a message structure
 *  @param[in] buf Pointer to a buffer containing the message
 *  @param[in] len Length of the buffer
 *
 *  @returns Number of bytes parsed or error code
 *  @retval >0 Number of bytes parsed
 *  @retval -EBADMSG The message is corrupt
 */
static int coap_msg_parse_token(coap_msg_t *msg, char *buf, unsigned len)
{
    if (len < msg->token_len)
    {
        return -EBADMSG;
    }
    memcpy(msg->token, buf, msg->token_len);
    return msg->token_len;
}

/**
 *  @brief Parse an option in a message
 *
 *  @param[out] msg Pointer to a message structure
 *  @param[in] buf Pointer to a buffer containing the message
 *  @param[in] len Length of the buffer
 *
 *  @returns Number of bytes parsed or error code
 *  @retval >0 Number of bytes parsed
 *  @retval -EBADMSG The message is corrupt
 */
static int coap_msg_parse_op(coap_msg_t *msg, char *buf, unsigned len)
{
    coap_msg_op_t *prev = NULL;
    unsigned op_delta = 0;
    unsigned op_len = 0;
    unsigned op_num = 0;
    char *p = buf;
    int ret = 0;

    if (len < 1)
    {
        return -EBADMSG;
    }
    op_delta = (p[0] >> 4) & 0x0f;
    op_len = p[0] & 0x0f;
    if ((op_delta == 15) || (op_len == 15))
    {
        return -EBADMSG;
    }
    p++;
    len--;
    if (op_delta == 13)
    {
        if (len < 1)
        {
            return -EBADMSG;
        }
        op_delta += p[0];
        p++;
        len--;
    }
    else if (op_delta == 14)
    {
        if (len < 2)
        {
            return -EBADMSG;
        }
        op_delta = 269 + ntohs(*((uint16_t *)(&p[0])));
        p += 2;
        len -= 2;
    }
    if (op_len == 13)
    {
        if (len < 1)
        {
            return -EBADMSG;
        }
        op_len += p[0];
        p++;
        len--;
    }
    else if (op_len == 14)
    {
        if (len < 2)
        {
            return -EBADMSG;
        }
        op_len = 269 + ntohs(*((uint16_t *)(&p[0])));
        p += 2;
        len -= 2;
    }
    if (len < op_len)
    {
        return -EBADMSG;
    }
    prev = coap_msg_op_list_get_last(&msg->op_list);
    if (prev == NULL)
    {
        op_num = op_delta;
    }
    else
    {
        op_num = coap_msg_op_get_num(prev) + op_delta;
    }
    ret = coap_msg_op_list_add_last(&msg->op_list, op_num, op_len, p);
    if (ret < 0)
    {
        return ret;
    }
    p += op_len;
    return p - buf;
}

/**
 *  @brief Parse the options in a message
 *
 *  @param[out] msg Pointer to a message structure
 *  @param[in] buf Pointer to a buffer containing the message
 *  @param[in] len Length of the buffer
 *
 *  @returns Number of bytes parsed or error code
 *  @retval >0 Number of bytes parsed
 *  @retval -EBADMSG The message is corrupt
 */
static int coap_msg_parse_ops(coap_msg_t *msg, char *buf, unsigned len)
{
    char *p = buf;
    int num = 0;

    while (1)
    {
        if ((p[0] == -1) || (len == 0))
        {
            break;
        }
        num = coap_msg_parse_op(msg, p, len);
        if (num < 0)
        {
            return num;
        }
        p += num;
        len -= num;
    }
    return p - buf;
}

/**
 *  @brief Parse the payload in a message
 *
 *  @param[out] msg Pointer to a message structure
 *  @param[in] buf Pointer to a buffer containing the message
 *  @param[in] len Length of the buffer
 *
 *  @returns Number of bytes parsed or error code
 *  @retval >0 Number of bytes parsed
 *  @retval -ENOMEM Out-of-memory
 *  @retval -EBADMSG The message is corrupt
 */
static int coap_msg_parse_payload(coap_msg_t *msg, char *buf, unsigned len)
{
    char *p = buf;

    if (len == 0)
    {
        return 0;
    }
    if (p[0] != -1)
    {
        return -EBADMSG;
    }
    p++;
    len--;
    if (len == 0)
    {
        return -EBADMSG;
    }
    msg->payload = (char *)malloc(len);
    if (msg->payload == NULL)
    {
        return -ENOMEM;
    }
    memcpy(msg->payload, p, len);
    msg->payload_len = len;
    p += len;
    return p - buf;
}

int coap_msg_parse(coap_msg_t *msg, char *buf, unsigned len)
{
    char *p = buf;
    int num = 0;

    coap_msg_destroy(msg);
    num = coap_msg_parse_hdr(msg, p, len);
    if (num < 0)
    {
        coap_msg_destroy(msg);
        return num;
    }
    p += num;
    len -= num;
    num = coap_msg_parse_token(msg, p, len);
    if (num < 0)
    {
        coap_msg_destroy(msg);
        return num;
    }
    p += num;
    len -= num;
    num = coap_msg_parse_ops(msg, p, len);
    if (num < 0)
    {
        coap_msg_destroy(msg);
        return num;
    }
    p += num;
    len -= num;
    num = coap_msg_parse_payload(msg, p, len);
    if (num < 0)
    {
        coap_msg_destroy(msg);
        return num;
    }
    return coap_msg_check(msg);
}

int coap_msg_set_type(coap_msg_t *msg, unsigned type)
{
    if ((type != COAP_MSG_CON)
     && (type != COAP_MSG_NON)
     && (type != COAP_MSG_ACK)
     && (type != COAP_MSG_RST))
    {
        return -EINVAL;
    }
    msg->type = type;
    return 0;
}

int coap_msg_set_code(coap_msg_t *msg, unsigned code_class, unsigned code_detail)
{
    if (code_class > COAP_MSG_MAX_CODE_CLASS)
    {
        return -EINVAL;
    }
    if (code_detail > COAP_MSG_MAX_CODE_DETAIL)
    {
        return -EINVAL;
    }
    msg->code_class = code_class;
    msg->code_detail = code_detail;
    return 0;
}

int coap_msg_set_msg_id(coap_msg_t *msg, unsigned msg_id)
{
    if (msg_id > COAP_MSG_MAX_MSG_ID)
    {
        return -EINVAL;
    }
    msg->msg_id = msg_id;
    return 0;
}

int coap_msg_set_token(coap_msg_t *msg, char *buf, unsigned len)
{
    if (len > COAP_MSG_MAX_TOKEN_LEN)
    {
        return -EINVAL;
    }
    memcpy(msg->token, buf, len);
    msg->token_len = len;
    return 0;
}

int coap_msg_add_op(coap_msg_t *msg, unsigned num, unsigned len, char *val)
{
    return coap_msg_op_list_add(&msg->op_list, num, len, val);
}

int coap_msg_set_payload(coap_msg_t *msg, char *buf, unsigned len)
{
    msg->payload_len = 0;
    if (msg->payload != NULL)
    {
        free(msg->payload);
        msg->payload = NULL;
    }
    if (len > 0)
    {
        msg->payload = (char *)malloc(len);
        if (msg->payload == NULL)
        {
            return -ENOMEM;
        }
        memcpy(msg->payload, buf, len);
        msg->payload_len = len;
    }
    return 0;
}

/**
 *  @brief Format the header in a message
 *
 *  @param[in] msg Pointer to a message structure
 *  @param[out] buf Pointer to a buffer to contain the formatted message
 *  @param[in] len Length of the buffer
 *
 *  @returns Length of the formatted message or error code
 *  @retval >0 Length of the formatted message
 *  @retval -ENOSPC Insufficient buffer length
 */
static int coap_msg_format_hdr(coap_msg_t *msg, char *buf, unsigned len)
{
    uint16_t msg_id = 0;

    if (len < 4)
    {
        return -ENOSPC;
    }
    buf[0] = (char)((COAP_MSG_VER << 6)
                  | ((msg->type & 0x03) << 4)
                  | (msg->token_len & 0x0f));
    buf[1] = (char)(((msg->code_class & 0x07) << 5)
                  | (msg->code_detail & 0x1f));
    msg_id = htons(msg->msg_id);
    memcpy(&buf[2], &msg_id, 2);
    return 4;
}

/**
 *  @brief Format the token in a message
 *
 *  @param[in] msg Pointer to a message structure
 *  @param[out] buf Pointer to a buffer to contain the formatted message
 *  @param[in] len Length of the buffer
 *
 *  @returns Length of the formatted message or error code
 *  @retval >0 Length of the formatted message
 *  @retval -ENOSPC Insufficient buffer length
 */
static int coap_msg_format_token(coap_msg_t *msg, char *buf, unsigned len)
{
    if (len < msg->token_len)
    {
        return -ENOSPC;
    }
    memcpy(buf, msg->token, msg->token_len);
    return msg->token_len;
}

/**
 *  @brief Format an option in a message
 *
 *  @param[in] op Pointer to an option structure
 *  @param[in] prev_num option number of the previous option
 *  @param[out] buf Pointer to a buffer to contain the formatted message
 *  @param[in] len Length of the buffer
 *
 *  @returns Length of the formatted message or error code
 *  @retval >0 Length of the formatted message
 *  @retval -ENOSPC Insufficient buffer length
 */
static int coap_msg_format_op(coap_msg_op_t *op, unsigned prev_num, char *buf, unsigned len)
{
    unsigned op_delta = 0;
    unsigned num = 0;
    uint16_t val = 0;
    char *p = buf;

    op_delta = op->num - prev_num;
    num++;

    /* option delta */
    if (op_delta >= 269)
    {
        num += 2;
    }
    else if (op_delta >= 13)
    {
        num += 1;
    }

    /* option length */
    if (op->len >= 269)
    {
        num += 2;
    }
    else if (op->num >= 13)
    {
        num += 1;
    }

    /* option value */
    num += op->len;
    if (num > len)
    {
        return -ENOSPC;
    }

    /* option delta */
    if (op_delta >= 269)
    {
        p[0] = 14 << 4;
    }
    else if (op_delta >= 13)
    {
        p[0] = 13 << 4;
    }
    else
    {
        p[0] = op_delta << 4;
    }

    /* option length */
    if (op->len >= 269)
    {
        p[0] |= 14;
    }
    else if (op->len >= 13)
    {
        p[0] |= 13;
    }
    else
    {
        p[0] |= op->len;
    }
    p++;
    len--;

    /* option delta extended */
    if (op_delta >= 269)
    {
        val = htons(op_delta - 269);
        memcpy(p, &val, 2);
        p += 2;
        len -= 2;
    }
    else if (op_delta >= 13)
    {
        p[0] = op_delta - 13;
        p++;
        len--;
    }

    /* option length extended */
    if (op->len >= 269)
    {
        val = htons(op->len - 269);
        memcpy(p, &val, 2);
        p += 2;
        len -= 2;
    }
    else if (op->len >= 13)
    {
        p[0] = op->len - 13;
        p++;
        len--;
    }

    /* option value */
    memcpy(p, op->val, op->len);
    p += op->len;

    return p - buf;
}

/**
 *  @brief Format the options in a message
 *
 *  @param[in] msg Pointer to a message structure
 *  @param[out] buf Pointer to a buffer to contain the formatted message
 *  @param[in] len Length of the buffer
 *
 *  @returns Length of the formatted message or error code
 *  @retval >0 Length of the formatted message
 *  @retval -ENOSPC Insufficient buffer length
 */
static int coap_msg_format_ops(coap_msg_t *msg, char *buf, unsigned len)
{
    coap_msg_op_t *op = NULL;
    unsigned prev_num = 0;
    char *p = buf;
    int num = 0;

    op = coap_msg_op_list_get_first(&msg->op_list);
    while (op != NULL)
    {
        num = coap_msg_format_op(op, prev_num, p, len);
        if (num < 0)
        {
            return num;
        }
        p += num;
        len -= num;
        prev_num = coap_msg_op_get_num(op);
        op = coap_msg_op_get_next(op);
    }
    return p - buf;
}

/**
 *  @brief Format the payload in a message
 *
 *  @param[in] msg Pointer to a message structure
 *  @param[out] buf Pointer to a buffer to contain the formatted message
 *  @param[in] len Length of the buffer
 *
 *  @returns Length of the formatted message or error code
 *  @retval >0 Length of the formatted message
 *  @retval -ENOSPC Insufficient buffer length
 */
static int coap_msg_format_payload(coap_msg_t *msg, char *buf, unsigned len)
{
    if (msg->payload_len == 0)
    {
        return 0;
    }
    if (msg->payload_len + 1 > len)
    {
        return -ENOSPC;
    }
    buf[0] = 0xff;
    memcpy(&buf[1], msg->payload, msg->payload_len);
    return msg->payload_len + 1;
}

int coap_msg_format(coap_msg_t *msg, char *buf, unsigned len)
{
    char *p = buf;
    int num = 0;
    int ret = 0;

    ret = coap_msg_check(msg);
    if (ret != 0)
    {
        return ret;
    }
    num = coap_msg_format_hdr(msg, p, len);
    if (num < 0)
    {
        return num;
    }
    p += num;
    len -= num;
    num = coap_msg_format_token(msg, p, len);
    if (num < 0)
    {
        return num;
    }
    p += num;
    len -= num;
    num = coap_msg_format_ops(msg, p, len);
    if (num < 0)
    {
        return num;
    }
    p += num;
    len -= num;
    num = coap_msg_format_payload(msg, p, len);
    if (num < 0)
    {
        return num;
    }
    p += num;
    return p - buf;
}

int coap_msg_copy(coap_msg_t *dst, coap_msg_t *src)
{
    coap_msg_op_t *op = NULL;
    int ret = 0;

    dst->ver = src->ver;
    ret = coap_msg_set_type(dst, coap_msg_get_type(src));
    if (ret < 0)
    {
        return ret;
    }
    ret = coap_msg_set_code(dst, coap_msg_get_code_class(src), coap_msg_get_code_detail(src));
    if (ret < 0)
    {
        return ret;
    }
    ret = coap_msg_set_msg_id(dst, coap_msg_get_msg_id(src));
    if (ret < 0)
    {
        return ret;
    }
    ret = coap_msg_set_token(dst, coap_msg_get_token(src), coap_msg_get_token_len(src));
    if (ret < 0)
    {
        return ret;
    }
    op = coap_msg_get_first_op(src);
    while (op != NULL)
    {
        ret = coap_msg_add_op(dst, coap_msg_op_get_num(op), coap_msg_op_get_len(op), coap_msg_op_get_val(op));
        if (ret < 0)
        {
            return ret;
        }
        op = coap_msg_op_get_next(op);
    }
    ret = coap_msg_set_payload(dst, coap_msg_get_payload(src), coap_msg_get_payload_len(src));
    if (ret < 0)
    {
        return ret;
    }
    return 0;
}
