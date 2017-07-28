#include "mbed.h"
#include "easy-connect.h"
#include "http_server.h"
#include "http_request.h"

Serial pc(USBTX, USBRX);

#define HTTP_STATUS_LINE "HTTP/1.0 200 OK"
#define HTTP_HEADER_FIELDS "Content-Type: text/html; charset=utf-8"
#define HTTP_MESSAGE_BODY ""                                     \
"<html>" "\r\n"                                                  \
"  <body style=\"display:flex;text-align:center\">" "\r\n"       \
"    <div style=\"margin:auto\">" "\r\n"                         \
"      <h1>Hello World</h1>" "\r\n"                              \
"      <p>It works !</p>" "\r\n"                                 \
"    </div>" "\r\n"                                              \
"  </body>" "\r\n"                                               \
"</html>"

#define HTTP_RESPONSE HTTP_STATUS_LINE "\r\n"   \
                      HTTP_HEADER_FIELDS "\r\n" \
                      "\r\n"                    \
                      HTTP_MESSAGE_BODY "\r\n"

void dump_response(HttpResponse* res) {
    printf("URL: %s\n", res->get_url().c_str());
    printf("Method: %s\n", http_method_str(res->get_method()));

    printf("Headers:\n");
    for (size_t ix = 0; ix < res->get_headers_length(); ix++) {
        printf("\t%s: %s\n", res->get_headers_fields()[ix]->c_str(), res->get_headers_values()[ix]->c_str());
    }
    printf("\nBody (%d bytes):\n\n%s\n", res->get_body_length(), res->get_body_as_string().c_str());
}

void request_handler(HttpResponse* request, TCPSocket* socket) {
    printf("URL: %s\n", request->get_url().c_str());
    printf("Method: %s\n", http_method_str(request->get_method()));

    printf("who is socket %p\n", socket);

    socket->send(HTTP_RESPONSE, strlen(HTTP_RESPONSE));
    // socket->close();

    // if (request->get_method() == HTTP_GET && request->get_url().compare("/") == 0) {
    // socket->send(HTTP_RESPONSE, strlen(HTTP_RESPONSE));
    // }
}

int main() {
    pc.baud(115200);
    // Connect to the network (see mbed_app.json for the connectivity method used)
    NetworkInterface* network = easy_connect(true);
    if (!network) {
        printf("Cannot connect to the network, see serial output\n");
        return 1;
    }

    // HttpServer server(network);
    // nsapi_error_t res = server.start(8080, &request_handler);

    // if (res == NSAPI_ERROR_OK) {
    //     printf("Server is listening at http://%s:8080\n", network->get_ip_address());
    // }
    // else {
    //     printf("Server could not be started... %d\n", res);
    // }

    TCPServer server(network);
    server.bind(8080);  // port 8080


    printf("Server is listening at http://%s:8080\n", network->get_ip_address());

    while (1) {
        TCPSocket clt_sock;
        SocketAddress clt_addr;

        printf("Waiting for incoming socket...\n");
        nsapi_error_t accept_res = server.accept(&clt_sock, &clt_addr);
        if (accept_res == NSAPI_ERROR_OK) {
            printf("Accepted...\n");
            HttpResponse response;
            HttpParser parser(&response, HTTP_REQUEST);

            // Set up a receive buffer (on the heap)
            uint8_t* recv_buffer = (uint8_t*)malloc(HTTP_RECEIVE_BUFFER_SIZE);

            // TCPSocket::recv is called until we don't have any data anymore
            nsapi_size_or_error_t recv_ret;
            printf("Calling recv\n");
            while ((recv_ret = clt_sock.recv(recv_buffer, HTTP_RECEIVE_BUFFER_SIZE)) > 0) {

                recv_buffer[recv_ret] = 0;
                printf("Buffer is %s\n", recv_buffer);

                // Pass the chunk into the http_parser
                size_t nparsed = parser.execute((const char*)recv_buffer, recv_ret);
                if (nparsed != recv_ret) {
                    printf("Parsing failed... parsed %d bytes, received %d bytes\n", nparsed, recv_ret);
                    // error = -2101;
                    free(recv_buffer);
                    return -1;
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
                return -1;
            }

            // When done, call parser.finish()
            parser.finish();

            // Free the receive buffer
            free(recv_buffer);

            printf("yay i read the request...\n");
            dump_response(&response);

            clt_sock.send(HTTP_RESPONSE, strlen(HTTP_RESPONSE));
        }

        wait_ms(0);

    }

    while (1) {
        // printf("hoi\n");
        wait_ms(500);
    }

    wait(osWaitForever);
}
