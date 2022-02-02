
// Copyright (c) 2020 The SuperNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef __KOMODO_WEBSOCKETS_H__
#define __KOMODO_WEBSOCKETS_H__

#include <boost/thread.hpp>

// The ASIO_STANDALONE define is necessary to use the standalone version of Asio.
// Remove if you are using Boost Asio.
// #define ASIO_STANDALONE
//#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/endpoint.hpp>
#include <websocketpp/connection.hpp>
//#include <websocketpp/extensions/permessage_deflate/enabled.hpp>

//using websocketpp::lib::bind;

static const int WSADDR_VERSION = 170008;
#define WEBSOCKETS_TIMEOUT_INTERVAL 120


//struct wsserver_mt_config : public websocketpp::config::asio {  // no tls
struct wsserver_mt_config : public websocketpp::config::asio_tls {  // tls

    // pull default settings from our core config
    static bool const enable_multithreading = true;

    struct transport_config : public websocketpp::config::core::transport_config {
        static bool const enable_multithreading = true;
    };
        
    /// permessage_compress extension
    //struct permessage_deflate_config {};

    //typedef websocketpp::extensions::permessage_deflate::enabled
    //    <permessage_deflate_config> permessage_deflate_type;
};

//typedef websocketpp::server<wsserver_mt_config> wsserver;   // no tls
typedef websocketpp::server<websocketpp::config::asio_tls> wsserver;  // tls

//transport::asio::tls_socket::endpoint 

typedef websocketpp::client<websocketpp::config::asio_client> wsclient;


typedef websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context> context_ptr;

// See https://wiki.mozilla.org/Security/Server_Side_TLS for more details about
// the TLS modes. The code below demonstrates how to implement both the modern
enum tls_mode {
    MOZILLA_INTERMEDIATE = 1,
    MOZILLA_MODERN = 2
};

bool StartWebSockets(boost::thread_group& threadGroup);
void SetWebSocketsWarmupFinished();
void StopWebSockets();

int GetnScore(const CService& addr); //net.cpp

bool ProcessWsMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived);

#endif 