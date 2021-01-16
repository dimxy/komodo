// Copyright (c) 2020 The SuperNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//#include "httpserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <iostream>
#include <vector>

#include "main.h"
//#include "chainparams.h"
//#include "chainparamsbase.h"

//#include "compat.h"
#include "util.h"
#include "net.h"
//#include "netbase.h"
//#include "rpc/protocol.h" // For HTTP status codes
#include "sync.h"
//#include "ui_interface.h"
#include "utilstrencodings.h"



/*#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/function.hpp>*/


/*#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
#ifdef _XOPEN_SOURCE_EXTENDED
#include <arpa/inet.h>
#endif
#endif*/

/*#include <boost/algorithm/string/case_conv.hpp> // for to_lower()
#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>*/

#include <functional>

#ifndef ENABLE_WEBSOCKETS
#error "ENABLE_WEBSOCKETS not defined"
#endif

#include "komodo_websockets.h"

static bool bWebSocketsStarted = false; 
static bool fWebSocketsInWarmup = true;
static CCriticalSection cs_wsWarmup;

//typedef websocketpp::server<websocketpp::config::asio> wsserver;
//typedef websocketpp::server<wsserver_mt_config> wsserver;

//typedef std::map< websocketpp::connection_hdl, CNode*, std::owner_less<websocketpp::connection_hdl> > ConnNodeMapType;
typedef websocketpp::lib::shared_ptr<websocketpp::lib::thread> thread_ptr;
static std::vector<thread_ptr> wsThreads;

//static std::set<websocketpp::connection_hdl> vHdls;
//static CCriticalSection cs_vHdls;

//static boost::thread wsThread;
//static boost::thread_group wsThreadGroup;

//class CWebSocketServer;
///static std::vector<CWebSocketServer*> vWsServers;

//static std::vector<CNode*> vWsNodes; // websocket separate node list
//static CCriticalSection cs_vWsNodes;
//CCriticalSection cs_vWsServers;

//static CNode* FindWsNode(const CAddress& addr)
static CNode* FindWsNode(wsserver::connection_ptr conn_ptr)
{
    //LOCK(cs_vWsNodes);
    LOCK(cs_vNodes);
    BOOST_FOREACH(CNode* pnode, vNodes)
        if (pnode->wsconn_ptr == conn_ptr)
            return (pnode);
    return NULL;
}

static void RemoveWsNode(const CNode *pNode)
{
//  AssertLockHeld(cs_vWsNodes);
    AssertLockHeld(cs_vNodes);
    //vWsNodes.erase(remove(vWsNodes.begin(), vWsNodes.end(), pNode), vWsNodes.end());
    vNodes.erase(remove(vNodes.begin(), vNodes.end(), pNode), vNodes.end());

    delete pNode;
}

// requires LOCK(cs_vWsSend)
void WebSocketSendData(wsserver *pServer, websocketpp::connection_hdl hdl, CNode *pnode)
{
    std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();

    while (it != pnode->vSendMsg.end()) {
        const CSerializeData &data = *it;
        assert(data.size() > pnode->nSendOffset);
        websocketpp::lib::error_code ec;
        int nBytes = data.size() - pnode->nSendOffset;
        //pnode->pWsEndPoint->send(*pnode->pWsConnHdl, &data[pnode->nSendOffset], nBytes, websocketpp::frame::opcode::binary, ec);
        pServer->send(hdl, &data[pnode->nSendOffset], nBytes, websocketpp::frame::opcode::binary, ec);

        if (!ec) {
            pnode->nLastSend = GetTime();  // needed to prevent inactivity disconnect
            pnode->nSendBytes += nBytes;
            pnode->nSendOffset += nBytes;
            pnode->RecordBytesSent(nBytes);
            if (pnode->nSendOffset == data.size()) {
                pnode->nSendOffset = 0;
                pnode->nSendSize -= data.size();
                it++;
            } else {
                // could not send full message; stop sending more
                break;
            }
        } else {  // error
            // int nErr = WSAGetLastError();
            // if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS)  // no similar errs in websocketpp...
            {
                LogPrintf("websocket send error %d %s\n", ec.value(), ec.category().name());
                //pnode->CloseSocketDisconnect();
                // looks like no need to call close or something if errs:
                //wsserver::connection_ptr conn_ptr = pnode->pWsEndPoint->get_con_from_hdl(*pnode->pWsConnHdl);
                //if (conn_ptr)
                //    conn_ptr->close(...);
            }
            // couldn't send anything at all
            break;
        }
    }

    if (it == pnode->vSendMsg.end()) {
        assert(pnode->nSendOffset == 0);
        assert(pnode->nSendSize == 0);
    }
    pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
}

class CWebSocketServer {
public:
    bool init() {

        LogPrintf("starting websocket listener...\n");

        try {

            // Set logging settings
            m_endpoint.set_error_channels(websocketpp::log::elevel::all);
            m_endpoint.set_access_channels(websocketpp::log::alevel::all ^ websocketpp::log::alevel::frame_payload);

            // Initialize Asio
            m_endpoint.init_asio();
            m_endpoint.set_reuse_addr(true);  // this would prevent komodod restart failure on the socket in use 
                                              // bcz the ws socket might be in CLOSE_WAIT state for a long time komodod has finished (especially if local app has not close the socket), 
                                              // however this is not recommended as allows for other apps to  use this socket as shared (security issue)
                                              // looks like still a websocketpp issue with socket shutdown 

            // Set the default message handler to the echo handler
            m_endpoint.set_message_handler(std::bind(
                &CWebSocketServer::message_handler, this,
                std::placeholders::_1, std::placeholders::_2
            ));

            m_endpoint.set_open_handler(bind(&CWebSocketServer::on_open, this, _1));
            m_endpoint.set_close_handler(bind(&CWebSocketServer::on_close, this, _1));
            m_endpoint.set_validate_handler(bind(&CWebSocketServer::on_validate, this, _1));
        } 
        catch (websocketpp::exception const & e) {
            LogPrintf("websockets init failed: %s \n", e.what());
            return false;
        }
        return true;
    }
    
    bool run() 
    {
        try {
            // Listen on port 9002
            m_endpoint.listen(8192);

            // Queues a connection accept operation
            m_endpoint.start_accept();

            // Start the Asio io_service run loop
            //m_endpoint.run();
            for (int i = 0; i < 4; i ++)
                wsThreads.push_back(websocketpp::lib::make_shared<websocketpp::lib::thread>(&wsserver::run, &m_endpoint));
        } 
        catch (websocketpp::exception const & e) {
            LogPrintf("websockets run failed: %s \n", e.what());
            return false;
        }
        return true;
    }

    void message_handler(websocketpp::connection_hdl hdl, wsserver::message_ptr msg) {
        boost::this_thread::interruption_point();
        // write a new message
        //std::cerr << __func__ << " payload=" << HexStr(msg->get_payload()) << std::endl;
        //m_endpoint.send(hdl, msg->get_payload(), msg->get_opcode());
        

        CNode *pNode = FindWsNode(m_endpoint.get_con_from_hdl(hdl));
        //CNode *pNode = m_connections[hdl];
        if (!pNode) {
            
            //pNode->pWsEndPoint = &m_endpoint;
            //pNode->AddRef();
            /*{
                LOCK(cs_vWsNodes);
                vWsNodes.push_back(pNode);
            }*/
            std::cerr << __func__ << " pnode not found=" << std::endl;
            return;
        }

        CAddress addr = GetClientAddressFromHdl(hdl);
        std::cerr << __func__ << " pnode found=" << addr.ToString() << " id=" << pNode->id << std::endl;

        //pNode->pWsConnHdl = &hdl;
        //pNode->AddRef();
        LOCK(pNode->cs_vRecvMsg);
        if (pNode->ReceiveMsgBytes(msg->get_payload().c_str(), msg->get_payload().size())) {
            if (ProcessMessages(pNode)) {
                LOCK(pNode->cs_vSend);
                SendMessages(pNode, true);
                WebSocketSendData(&m_endpoint, hdl, pNode);
            }
            pNode->nLastRecv = GetTime(); // needed to prevent inactivity disconnect
            pNode->nRecvBytes += msg->get_payload().size();
            pNode->RecordBytesRecv(msg->get_payload().size());
        }
        else {
            LogPrintf("error websocket message processing, disconnecting peer %d\n", pNode->id);
            pNode->wsconn_ptr->close(websocketpp::close::status::unsupported_data, std::string());
        }
        //pNode->Release();
    }

    void stop()
    {
        LogPrintf("stopping websocket listener...\n");
        websocketpp::lib::error_code ec;
        m_endpoint.stop_listening(ec);

        LogPrintf("closing websocket connections...\n");
        {
            //LOCK(cs_vWsNodes);
            LOCK(cs_vNodes);
            //for (auto const &conn : m_connections)
            BOOST_FOREACH(CNode* pnode, vNodes)
            if (pnode->isWebSocket && pnode->wsconn_ptr != NULL)
            {
                //wsserver::connection_ptr conn_ptr = m_endpoint.get_con_from_hdl(conn.first);            
                pnode->wsconn_ptr->close(websocketpp::close::status::going_away, std::string());
            }
        }

        LogPrintf("waiting for websocket threads to stop... (Note that a possible 'asio async_shutdown error' message is an expected behaviour)\n"); //https://github.com/zaphoyd/websocketpp/issues/556
        for (size_t i = 0; i < wsThreads.size(); i++) {
            wsThreads[i]->join();
        }
        //m_endpoint.close();
        //LOCK(cs_vWsNodes);
        //for (auto const &pNode : vWsNodes)
        //    RemoveWsNode(pNode);
        while(!m_endpoint.stopped())
            MilliSleep(250);

    }

private:
    bool on_validate(websocketpp::connection_hdl hdl)
    {
        return !fWebSocketsInWarmup;
    }

    void on_open(websocketpp::connection_hdl hdl)
    {
        CAddress addr = GetClientAddressFromHdl(hdl);
        std::cerr << __func__ << " new pnode created=" << addr.ToString() << std::endl;

        CNode *pNode = new CNode(INVALID_SOCKET, addr, "", true);  // not used hSocket for websockets
        pNode->isWebSocket = true;
        pNode->wsconn_ptr = m_endpoint.get_con_from_hdl(hdl);
        pNode->AddRef();
        {
            LOCK(cs_vNodes);
            vNodes.push_back(pNode);
        }
        //m_connections[hdl] = pNode;
    }

    void on_close(websocketpp::connection_hdl hdl)
    {
        std::cerr << __func__ << " enterred" << std::endl;
        /*CAddress addr = GetClientAddressFromHdl(hdl);*/

        CNode *pNode = FindWsNode(m_endpoint.get_con_from_hdl(hdl));
        if (pNode) {
            LOCK(cs_vNodes);
            RemoveWsNode(pNode);  
        }
        //CNode *pNode = m_connections[hdl];
        //if (pNode)
        //    delete pNode;
        //m_connections.erase(hdl);
    }

    CAddress GetClientAddressFromHdl(websocketpp::connection_hdl hdl) 
    {
        wsserver::connection_ptr conn_ptr = m_endpoint.get_con_from_hdl(hdl);
        std::string sAddr = conn_ptr->get_remote_endpoint();
        //std::string host = conn_ptr->get_host();
        //uint16_t port = conn_ptr->get_port();
        //std::cerr << __func__ << " host=" << host << " port=" << port << std::endl;
        //CService svc(host, port);
        std::cerr << __func__ << " sAddr=" << sAddr << std::endl;
        CService svc(sAddr);
        return CAddress(svc);
    }

    wsserver m_endpoint;
    //ConnNodeMapType m_connections;
};

static CWebSocketServer webSocketServer;


/*static void RunWsServer()
{
    CWebSocketServer *pWsServer = new CWebSocketServer();
    pWsServer->init();
    pWsServer->run();
    {
        LOCK(cs_vWsServers);
        vWsServers.push_back(pWsServer);
    }
}*/


bool StartWebSockets() {
    //wssserver.init();
    //wsThread = boost::thread(RunWsServer);
    //for (int i = 0; i < 4; i ++)
    //    wsThreadGroup.create_thread(RunWsServer);
    //wssserver.run();
    if (!webSocketServer.init())
        return false;
    if (!webSocketServer.run())
        return false;
    bWebSocketsStarted = true;
    return true;
}

void SetWebSocketsWarmupFinished()
{
    LOCK(cs_wsWarmup);
    assert(fWebSocketsInWarmup);
    fWebSocketsInWarmup = false;
}


void StopWebSockets() 
{
    if (bWebSocketsStarted) {
        webSocketServer.stop();
        /*if (!vWsServers.empty()) 
        {
            LOCK(cs_vWsServers);

            std::cerr << __func__ << " stopping websocket listeners..." << std::endl;
            for (auto const &pWsServer : vWsServers) 
                pWsServer->stop();
            
            std::cerr << __func__ << " waiting for websocket threads to stop... (Note that possible 'asio async_shutdown error' is an expected behaviour)" << std::endl; //https://github.com/zaphoyd/websocketpp/issues/556
            wsThreadGroup.join_all();

            for (auto const &pWsServer : vWsServers) 
                delete pWsServer;
            vWsServers.clear();
        }*/
    }
}
