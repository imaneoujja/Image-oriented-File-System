/*
 * @file http_net.c
 * @brief HTTP server layer for CS-202 project
 *
 * @author Konstantinos Prasopoulos
 */

#include <stdlib.h>
#include <stdio.h>
#include <sys/socket.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#include "http_prot.h"
#include "http_net.h"
#include "socket_layer.h"
#include "imgfs_server_service.h"
#include "error.h"

static int passive_socket = -1;
static EventCallback cb = handle_http_message;

#define MK_OUR_ERR(X) \
static int our_ ## X = X

MK_OUR_ERR(ERR_NONE);
MK_OUR_ERR(ERR_INVALID_ARGUMENT);
MK_OUR_ERR(ERR_OUT_OF_MEMORY);
MK_OUR_ERR(ERR_IO);

static void* handle_connection(void *arg) { 
   if (arg == NULL) return &our_ERR_INVALID_ARGUMENT; 
       int client_fd = *(int *)arg;


    // buffer for the http header - used allocation so that I can assign new value to it
    char *rcvbuf = malloc(MAX_HEADER_SIZE);
    if (rcvbuf == NULL) return &our_ERR_OUT_OF_MEMORY;

    int read_bytes = 0;
    char *header_end = NULL;
    int content_len = 0; // ZAC: explicitly initialized content length to 0.
    int extended = 0;

    struct http_message message;

    do {
        ssize_t num_bytes_read = tcp_read(client_fd,
                                          rcvbuf + read_bytes,
                                          MAX_HEADER_SIZE - read_bytes - 1);
        if (num_bytes_read <= 0) {
            free(rcvbuf);
            rcvbuf = NULL;
            close(client_fd);
          
            return &our_ERR_IO;
        }
        read_bytes += (int)num_bytes_read;
        rcvbuf[read_bytes] = '\0'; // null terminate string for safety

        // search for the header delimiter
        header_end = strstr(rcvbuf, HTTP_HDR_END_DELIM);  // "\r\n\r\n"

        //=============================================WEEK 12==========================================================

        int ret_parsed_mess = http_parse_message(rcvbuf,read_bytes,&message, &content_len);
        if (ret_parsed_mess < 0) {
            free(rcvbuf); // parse_message returns negative if an error occurred (http_prot.h)
            rcvbuf = NULL;
            close(client_fd);

            return &our_ERR_IO;
        } else if (ret_parsed_mess == 0) { // partial treatment (see http_prot.h)
            if (!extended && content_len > 0 && read_bytes < content_len) {
                char *new_buf = realloc(rcvbuf, MAX_HEADER_SIZE + content_len);
                if (!new_buf) {
                    free(rcvbuf);
                    rcvbuf = NULL;
                    close(client_fd);

                    return &our_ERR_OUT_OF_MEMORY;
                }
                rcvbuf = new_buf;
                extended = 1;
            }
        } else { // case where the message was fully received and parsed
            int callback_result = cb(&message, client_fd);
            if (callback_result < 0) {
                free(rcvbuf);
                rcvbuf = NULL;
                close(client_fd);
              
                return &our_ERR_IO;
            } else {
                read_bytes = 0;
                content_len = 0;
                extended = 0;
                memset(rcvbuf, 0, MAX_HEADER_SIZE);
            }
        }
    } while (!header_end && read_bytes < MAX_HEADER_SIZE);  // do this until delimiter is found, or buffer is full

    free(rcvbuf);
    rcvbuf = NULL;
    close(client_fd);


  
    return &our_ERR_NONE;
}


/***********************
 * Init connection
 */
int http_init(uint16_t port, EventCallback callback)
{
    passive_socket = tcp_server_init(port);
    cb = callback;
    return passive_socket;
}

/***********************
 * Close connection
 */
void http_close(void)
{
    if (passive_socket > 0) {
        if (close(passive_socket) == -1)
            perror("close() in http_close()");
        else
            passive_socket = -1;
    }
}

/***********************
 * Receive content
 */
int http_receive(void) {
    int client_socket = tcp_accept(passive_socket);
    if (client_socket < 0) {
        close(passive_socket);
        return ERR_IO;
    }
    handle_connection((void*)&client_socket);

    return ERR_NONE;

}
/***********************
 * Serve a file content over HTTP
 */
int http_serve_file(int connection, const char* filename)
{
    int ret = ERR_NONE;
    return ret;
}

/***********************
 * Create and send HTTP reply
 */
int http_reply(int connection, const char* status, const char* headers, const char* body, size_t body_len) {
    M_REQUIRE_NON_NULL(status);
    M_REQUIRE_NON_NULL(headers);
    if (body_len !=0) M_REQUIRE_NON_NULL(body);

    const char* protocol = HTTP_PROTOCOL_ID;
    const char* content_length_text = "Content-Length: ";
    int body_len_chars = (body_len == 0) ? 1 : (int)(log10(body_len) + 1);

    size_t buffer_size = strlen(HTTP_PROTOCOL_ID)
                        + strlen(status)
                        + strlen(HTTP_LINE_DELIM)
                        + strlen(headers)
                        + strlen(content_length_text)
                        + body_len_chars
                        + strlen(HTTP_HDR_END_DELIM)
                        + body_len
                        + 1;

    char* buffer = (char*)calloc(buffer_size, 1);
    if (buffer == NULL) {
        return our_ERR_OUT_OF_MEMORY;
    }

    int written = snprintf(buffer, buffer_size, "%s%s%s%s%s%zu%s", protocol,
                           status, HTTP_LINE_DELIM, headers,
                           content_length_text, body_len, HTTP_HDR_END_DELIM);

    if (written < 0 || written >= buffer_size) {
        free(buffer);
        return our_ERR_INVALID_ARGUMENT;
    }

    if (body != NULL) {
        memcpy(buffer + written, body, body_len);
    }

    if (tcp_send(connection, buffer, written + body_len) == -1) {
        free(buffer);
        return our_ERR_IO;
    }

    free(buffer);
    return our_ERR_NONE;

 

}