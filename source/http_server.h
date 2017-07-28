/*
 * PackageLicenseDeclared: Apache-2.0
 * Copyright (c) 2017 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _HTTP_SERVER_
#define _HTTP_SERVER_

#include "mbed.h"
#include "http_request_parser.h"
#include "http_response.h"

#define HTTP_SERVER_MAX_CONCURRENT      5

TCPSocket* temp; // cant figure this stuff out

/**
 * \brief HttpServer implements the logic for setting up an HTTP server.
 */
class HttpServer {
public:
    /**
     * HttpRequest Constructor
     *
     * @param[in] network The network interface
    */
    HttpServer(NetworkInterface* network) : server(network) {

    }

    ~HttpServer() {
        for (size_t ix = 0; ix < HTTP_SERVER_MAX_CONCURRENT; ix++) {
            if (socket_threads[ix]) {
                delete socket_threads[ix];
            }
        }
    }

    /**
     * Start listening
     *
     * @param[in] port The port to listen on
     */
    nsapi_error_t bind(uint16_t port) {
        server.listen(HTTP_SERVER_MAX_CONCURRENT); // max. concurrent connections...
        return server.bind(port);
    }

    /**
     * Start running the server (it will run on it's own thread)
     */
    nsapi_error_t start(uint16_t port, Callback<void(HttpResponse* request, TCPSocket* socket)> a_handler) {
        server.listen(5); // max. concurrent connections...

        nsapi_error_t ret = server.bind(port);
        if (ret != NSAPI_ERROR_OK) {
            return ret;
        }

        handler = a_handler;

        main_thread.start(callback(this, &HttpServer::main));

        return NSAPI_ERROR_OK;
    }

private:

    void receive_data() {
        // UNSAFE: should Mutex around it or something
        TCPSocket* socket = sockets.back();

        HttpResponse response;
        HttpParser parser(&response, HTTP_REQUEST);

        // Set up a receive buffer (on the heap)
        uint8_t* recv_buffer = (uint8_t*)malloc(HTTP_RECEIVE_BUFFER_SIZE);

        // TCPSocket::recv is called until we don't have any data anymore
        nsapi_size_or_error_t recv_ret;
        while ((recv_ret = socket->recv(recv_buffer, HTTP_RECEIVE_BUFFER_SIZE)) > 0) {

            // Pass the chunk into the http_parser
            size_t nparsed = parser.execute((const char*)recv_buffer, recv_ret);
            if (nparsed != recv_ret) {
                printf("Parsing failed... parsed %d bytes, received %d bytes\n", nparsed, recv_ret);
                // error = -2101;
                free(recv_buffer);
                return;
            }

            if (response.is_message_complete()) {
                break;
            }
        }
        // error?
        if (recv_ret < 0) {
            printf("Could not read from socket... %d\n", recv_ret);
            // error = recv_ret;
            free(recv_buffer);
            return;
        }

        // When done, call parser.finish()
        parser.finish();

        // Free the receive buffer
        free(recv_buffer);

        handler(&response, socket);
    }

    void main() {
        wait_ms(1000);

        while (1) {
            TCPSocket* clt_sock = new TCPSocket(); // Q: when should these be cleared? When not connected anymore?
            printf("I has made socket %p\n", clt_sock);
            SocketAddress clt_addr;

            nsapi_error_t accept_res = server.accept(clt_sock, &clt_addr);
            if (accept_res == NSAPI_ERROR_OK) {
                sockets.push_back(clt_sock); // so we can clear our disconnected sockets

                // and start listening for events there
                Thread* t = new Thread(osPriorityNormal, 1024);
                t->start(callback(this, &HttpServer::receive_data));

                socket_threads.push_back(t);
            }

            for (size_t ix = 0; ix < socket_threads.size(); ix++) {
                if (socket_threads[ix]->get_state() == Thread::Deleted) {
                    delete socket_threads[ix];
                    socket_threads.erase(socket_threads.begin() + ix); // what if there are two?
                }
            }

        }
    }

    TCPServer server;
    Thread main_thread;
    vector<TCPSocket*> sockets;
    vector<Thread*> socket_threads;
    Callback<void(HttpResponse* request, TCPSocket* socket)> handler = 0;
};

#endif // _HTTP_SERVER
