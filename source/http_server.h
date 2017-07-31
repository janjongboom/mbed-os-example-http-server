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
#include "http_response_builder.h"

#ifndef HTTP_SERVER_MAX_CONCURRENT
#define HTTP_SERVER_MAX_CONCURRENT      5
#endif

typedef HttpResponse ParsedHttpRequest;

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
    HttpServer(NetworkInterface* network) {
        _network = network;
    }

    ~HttpServer() {
        for (size_t ix = 0; ix < HTTP_SERVER_MAX_CONCURRENT; ix++) {
            if (socket_threads[ix]) {
                delete socket_threads[ix];
            }
        }
    }

    /**
     * Start running the server (it will run on it's own thread)
     */
    nsapi_error_t start(uint16_t port, Callback<void(ParsedHttpRequest* request, TCPSocket* socket)> a_handler) {
        server = new TCPServer();

        nsapi_error_t ret;

        ret = server->open(_network);
        if (ret != NSAPI_ERROR_OK) {
            return ret;
        }

        ret = server->bind(port);
        if (ret != NSAPI_ERROR_OK) {
            return ret;
        }

        server->listen(HTTP_SERVER_MAX_CONCURRENT); // max. concurrent connections...

        handler = a_handler;

        main_thread.start(callback(this, &HttpServer::main));

        return NSAPI_ERROR_OK;
    }

private:

    void receive_data() {
        // UNSAFE: should Mutex around it or something
        TCPSocket* socket = sockets.back();

        // needs to keep running until the socket gets closed
        while (1) {

            ParsedHttpRequest* response = new ParsedHttpRequest();
            HttpParser* parser = new HttpParser(response, HTTP_REQUEST);

            // Set up a receive buffer (on the heap)
            uint8_t* recv_buffer = (uint8_t*)malloc(HTTP_RECEIVE_BUFFER_SIZE);

            // TCPSocket::recv is called until we don't have any data anymore
            nsapi_size_or_error_t recv_ret;
            while ((recv_ret = socket->recv(recv_buffer, HTTP_RECEIVE_BUFFER_SIZE)) > 0) {
                // Pass the chunk into the http_parser
                size_t nparsed = parser->execute((const char*)recv_buffer, recv_ret);
                if (nparsed != recv_ret) {
                    printf("Parsing failed... parsed %d bytes, received %d bytes\n", nparsed, recv_ret);
                    recv_ret = -2101;
                    break;
                }

                if (response->is_message_complete()) {
                    break;
                }
            }
            // error?
            if (recv_ret <= 0) {
                if (recv_ret < 0) {
                    printf("Error reading from socket %d\n", recv_ret);
                }

                // error = recv_ret;
                delete response;
                delete parser;
                free(recv_buffer);

                // q; should we always break out of the thread or only if NO_SOCKET ?
                // should we delete socket here? the socket seems already gone...
                if (recv_ret < -3000 || recv_ret == 0) {
                    return;
                }
                else {
                    continue;
                }
            }

            // When done, call parser.finish()
            parser->finish();

            // Free the receive buffer
            free(recv_buffer);

            // Let user application handle the request, if user needs a handle to response they need to memcpy themselves
            if (recv_ret > 0) {
                handler(response, socket);
            }

            // Free the response and parser
            delete response;
            delete parser;
        }
    }

    void main() {
        while (1) {
            TCPSocket* clt_sock = new TCPSocket(); // Q: when should these be cleared? When not connected anymore?
            SocketAddress clt_addr;

            nsapi_error_t accept_res = server->accept(clt_sock, &clt_addr);
            if (accept_res == NSAPI_ERROR_OK) {
                sockets.push_back(clt_sock); // so we can clear our disconnected sockets

                // and start listening for events there
                Thread* t = new Thread(osPriorityNormal, 2048);
                t->start(callback(this, &HttpServer::receive_data));

                socket_thread_metadata_t* m = new socket_thread_metadata_t();
                m->socket = clt_sock;
                m->thread = t;
                socket_threads.push_back(m);
            }
            else {
                delete clt_sock;
            }

            for (size_t ix = 0; ix < socket_threads.size(); ix++) {
                if (socket_threads[ix]->thread->get_state() == Thread::Deleted) {
                    socket_threads[ix]->thread->terminate();
                    delete socket_threads[ix]->thread;
                    delete socket_threads[ix]->socket; // does this work on esp8266?
                    socket_threads.erase(socket_threads.begin() + ix); // what if there are two?
                }
            }

        }
    }

    typedef struct {
        TCPSocket* socket;
        Thread*    thread;
    } socket_thread_metadata_t;

    TCPServer* server;
    NetworkInterface* _network;
    Thread main_thread;
    vector<TCPSocket*> sockets;
    vector<socket_thread_metadata_t*> socket_threads;
    Callback<void(ParsedHttpRequest* request, TCPSocket* socket)> handler;
};

#endif // _HTTP_SERVER
