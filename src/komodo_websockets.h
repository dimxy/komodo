
// Copyright (c) 2020 The SuperNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef __KOMODO_WEBSOCKETS_H__
#define __KOMODO_WEBSOCKETS_H__

#include <boost/thread.hpp>

// The ASIO_STANDALONE define is necessary to use the standalone version of Asio.
// Remove if you are using Boost Asio.
// #define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/endpoint.hpp>
#include <websocketpp/connection.hpp>
//#include <websocketpp/extensions/permessage_deflate/enabled.hpp>

static const int WSADDR_VERSION = 170008;
#define WEBSOCKETS_TIMEOUT_INTERVAL 120


struct wsserver_mt_config : public websocketpp::config::asio {
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

typedef websocketpp::server<wsserver_mt_config> wsserver;
typedef websocketpp::client<websocketpp::config::asio_client> wsclient;

bool StartWebSockets(boost::thread_group& threadGroup);
void SetWebSocketsWarmupFinished();
void StopWebSockets();

int GetnScore(const CService& addr); //net.cpp

#endif 