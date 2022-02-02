// Copyright (c) 2020 The SuperNet developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// websockets support for komodod
// based on websocketspp library
// introduces new commands getwsaddr/wsaddr to get list of websockets listener
// to build ws listener table a object of CAddrMan is used, wsaddrman
// generally the same protocol is used like for the original p2p addrman:
//  


//#include "httpserver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>

#include <iostream>
#include <vector>
#include <list>
#include <set>

#include "timedata.h"
#include "main.h"
#include "consensus/validation.h"
#include "util.h"
#include "net.h"
#include "addrman.h"
#include "sync.h"
#include "utilstrencodings.h"
#include "univalue.h"
#include "rpc/server.h"

//#include <functional>

#ifndef ENABLE_WEBSOCKETS
#error "ENABLE_WEBSOCKETS not defined"
#endif

#include "komodo_websockets.h"

static CAddrMan wsaddrman;

static bool bWebSocketsStarted = false; 
static bool fWebSocketsInWarmup = true;
static bool fWebSocketsStopping = false;

static CCriticalSection cs_wsWarmup;

static CSemaphore *semWsOutbound = NULL;
static boost::condition_variable wsMessageHandlerCondition;


static std::set<CNetAddr> setservAddNodeWsAddresses;
static CCriticalSection cs_setservAddNodeWsAddresses;

static std::vector<std::string> vAddedWsNodes;
static CCriticalSection cs_vAddedWsNodes;

typedef websocketpp::lib::shared_ptr<websocketpp::lib::thread> ws_thread_ptr;
static std::vector<ws_thread_ptr> vWsThreads;

static boost::thread_group wsThreadGroup;

unsigned short GetWebSocketListenPort()
{
    return (unsigned short)(GetArg("-wsport", 8192));
}

CAddress GetLocalWebSocketAddress(const CNetAddr *paddrPeer)
{
    CAddress ret(CService("0.0.0.0", GetWebSocketListenPort()), 0);
    // TODO: decide if we need websocket listeners bound to specific local address (not 0.0.0.0)
    CService addr;
    if (GetLocal(addr, paddrPeer))
    {
        ret = CAddress(addr);
    }
    ret.nServices = NODE_NETWORK | NODE_WEBSOCKETS;
    ret.nTime = GetTime();
    return ret;
}

// advertizes websockets listen address
void AdvertizeLocalWebSockets(CNode *pnode)
{
    if (/*fListen && <-- TODO: add this flag*/ pnode->fSuccessfullyConnected)
    {
        CAddress addrLocal = GetLocalWebSocketAddress(&pnode->addr);
        // If discovery is enabled, sometimes give our peer the address it
        // tells us that it sees us as in case it has a better idea of our
        // address than we do.
        if (IsPeerAddrLocalGood(pnode) && (!addrLocal.IsRoutable() ||
             GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8:2) == 0))
        {
            addrLocal.SetIP(pnode->addrLocal);
        }
        if (addrLocal.IsRoutable())
        {
            LogPrintf("AdvertizeLocalWebSockets: advertizing local websocket address %s for peer %d\n", addrLocal.ToString(), pnode->id);
            pnode->PushAddress(addrLocal);
        }
    }
}

class CWsNode;

// base wrapper class both for server and outbound endpoints
class CWsEndpointWrapper {
public:
    CWsEndpointWrapper() {}
    virtual void send(websocketpp::connection_hdl hdl, void const * payload, size_t len,
        websocketpp::frame::opcode::value op, websocketpp::lib::error_code & ec) = 0;

    virtual void close(websocketpp::connection_hdl hdl, websocketpp::close::status::value) = 0;
    virtual void sendWsData(CWsNode *pNode) = 0;
};

typedef std::shared_ptr<CWsEndpointWrapper> ws_endpoint_ptr;
static ws_endpoint_ptr spWebSocketServer;

class CWsNode : public CNode {
public:
    CWsNode(SOCKET hSocketIn, const CAddress &addrIn, const std::string &addrNameIn = "", bool fInboundIn = false)
        : CNode(hSocketIn, addrIn, addrNameIn, fInboundIn)
    {        
        closeErrorOnSend = 0;
        closeErrorOnReceive = 0;
        nLastRebroadcast = 0;
    }

    websocketpp::connection_hdl m_hdl;
    ws_endpoint_ptr m_spWsEndpoint;
    websocketpp::close::status::value closeErrorOnSend;
    websocketpp::close::status::value closeErrorOnReceive;
    int64_t nLastRebroadcast; // for rebroacasting local address

    void PushWsVersion()
    {
        int nBestHeight = GetNodeSignals().GetHeight().get_value_or(0);

        int64_t nTime = (fInbound ? GetTime() : GetTime());
        CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0",0)));
        CAddress addrMe = GetLocalWebSocketAddress(&addr);
        GetRandBytes((unsigned char*)&nLocalHostNonce, sizeof(nLocalHostNonce));
        if (fLogIPs)
            LogPrint("websockets", "send websocket version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), addrYou.ToString(), id);
        else
            LogPrint("websockets", "send websocket version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION, nBestHeight, addrMe.ToString(), id);
        PushMessage("version", PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe,
                    nLocalHostNonce, strSubVersion, nBestHeight, true);
    }

    ~CWsNode() {
        wsaddrman.Connected(addr);
    }
};

typedef std::shared_ptr<CWsNode> CWsNodePtr;
static std::vector<CWsNodePtr> vWsNodes; // websocket own node list
static CCriticalSection cs_vWsNodes;

static std::set<CWsNodePtr> vWsNodesDisconnected; // websocket disconnected nodes
static CCriticalSection cs_vWsNodesDisconnected;

class CWebSocketOutbound;
static std::vector<ws_endpoint_ptr> vOutboundEndpoints; // wait until enpoint opens
static CCriticalSection cs_vOutboundEndpoints;

static CWsNodePtr FindWsNode(const CNetAddr& ip)
{
    LOCK(cs_vWsNodes);
    for(auto const & pnode : vWsNodes)
        if ((CNetAddr)pnode->addr == ip)
            return (pnode);
    return NULL;
}

static CWsNodePtr FindWsNode(const CSubNet& subNet)
{
    LOCK(cs_vWsNodes);
    for(auto const & pnode : vWsNodes)
    if (subNet.Match((CNetAddr)pnode->addr))
        return (pnode);
    return NULL;
}

static CWsNodePtr FindWsNode(const std::string& addrName)
{
    LOCK(cs_vWsNodes);
    for(auto const & pnode : vWsNodes)
        if (pnode->addrName == addrName)
            return (pnode);
    return NULL;
}

static CWsNodePtr FindWsNode(const CService& addr)
{
    LOCK(cs_vWsNodes);
    for(auto const & pnode : vWsNodes)
        if ((CService)pnode->addr == addr)
            return (pnode);
    return NULL;
}


static void RemoveWsNode(CWsNodePtr pNode)
{
    AssertLockHeld(cs_vWsNodes);
    vWsNodes.erase(std::remove(vWsNodes.begin(), vWsNodes.end(), pNode), vWsNodes.end());
    LOCK(cs_vWsNodesDisconnected);
    vWsNodesDisconnected.insert(pNode);
}

// returns true if message was recognized
bool ProcessWsMessage(CNode* pfrom, std::string strCommand, CDataStream& vRecv, int64_t nTimeReceived)
{

    if (strCommand == "version")
    {            
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->PushMessage("reject", strCommand, REJECT_DUPLICATE, std::string("Duplicate version message"));
            Misbehaving(pfrom->GetId(), 1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        int nVersion;           // use temporary for version, don't set version number until validated as connected
        int minVersion = MIN_PEER_PROTO_VERSION;

        //if ( is_STAKED(ASSETCHAINS_SYMBOL) != 0 )
        //    minVersion = STAKEDMIN_PEER_PROTO_VERSION;
        vRecv >> nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (nVersion == 10300)
            nVersion = 300;
        if (nVersion < minVersion)
        {
            // disconnect from peers older than this proto version
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, pfrom->nVersion);
            pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater", minVersion));
            pfrom->fDisconnect = true;
            return false;
        }

        // not relevant to websockets:
        // Reject incoming connections from nodes that don't know about the current epoch
        /*const Consensus::Params& params = Params().GetConsensus();
        auto currentEpoch = CurrentEpoch(GetHeight(), params);
        if (nVersion < params.vUpgrades[currentEpoch].nProtocolVersion)
        {
            LogPrintf("peer=%d using obsolete version %i; disconnecting\n", pfrom->id, nVersion);
            pfrom->PushMessage("reject", strCommand, REJECT_OBSOLETE,
                            strprintf("Version must be %d or greater",
                            params.vUpgrades[currentEpoch].nProtocolVersion));
            pfrom->fDisconnect = true;
            return false;
        }*/
        
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty()) {
            vRecv >> LIMITED_STRING(pfrom->strSubVer, MAX_SUBVERSION_LENGTH);
            pfrom->cleanSubVer = SanitizeString(pfrom->strSubVer);
        }
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;
        
        // not relevant to websockets:
        /* if (!vRecv.empty())
            vRecv >> pfrom->fRelayTxes; // set to true after we get the first filter* message
        else
            pfrom->fRelayTxes = true;*/

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            LogPrintf("websockets connected to self at %s, disconnecting\n", pfrom->addr.ToString());
            pfrom->fDisconnect = true;
            return true;
        }

        pfrom->nVersion = nVersion;

        pfrom->addrLocal = addrMe;
        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            // SeenLocal(addrMe); // TODO: better not to influence the main p2p net
        }

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            static_cast<CWsNode*>(pfrom)->PushWsVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        // not relevant to websockets
        // Potentially mark this peer as a preferred download peer.
        //UpdatePreferredDownload(pfrom, State(pfrom->GetId()));

        // Change version
        pfrom->PushMessage("verack");
        pfrom->ssSend.SetVersion(std::min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound) 
        {

            // Advertise our ws address
            // but only if this node connected to remote (outbound) nodes
            // note that because of that a listening node does not advertise its local address on "version" cmd
            // also the outbound node will get back its local white address only on the next addr message
            // the outbound node will get the next "wsaddr" on "getwsaddr" response 
            // (note that the first "getaddr" for the first outbound will return none as no any advertised local addr yet on the listening node) 
            // plus "wsaddr" cmd is sent in SendMessages will also have a empty address list to send
            // Note, that "addrMe" from a remote node from "version" is not added to AddrLocal 
            if (!IsInitialBlockDownload()) // TODO: isWebsocketStarted
            {
                // get best real local websocket listening address (known outside) and advertize it
                CAddress addr = GetLocalWebSocketAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    LogPrintf("%s: advertizing local websocket address %s for peer %d (from local)\n", __func__, addr.ToString(), pfrom->GetId());
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    LogPrintf("%s: advertizing local websocket address %s for peer %d (from node)\n", __func__, addr.ToString(), pfrom->GetId());
                    pfrom->PushAddress(addr);
                }
            }
            else
            {
                LogPrint("websockets", "note: websockets local address advertising skipped: IsInitialBlockDownload()=%d\n", IsInitialBlockDownload());
            }

            // Get recent websocket addresses
            if (pfrom->fOneShot || pfrom->nVersion >= WSADDR_VERSION || wsaddrman.size() < 1000)
            {
                LogPrint("websockets", "pushing getwsaddr request for peer=%d\n", pfrom->GetId());
                pfrom->PushMessage("getwsaddr");
            }
            wsaddrman.Good(pfrom->addr);
        } 
        else 
        {
            std::cerr << __func__ << " version from inbound pfrom->addr=" << pfrom->addr.ToStringIPPort() << " (CNetAddr)pfrom->addr=" << ((CNetAddr)pfrom->addr).ToString() << " (CNetAddr)addrFrom=" << ((CNetAddr)addrFrom).ToString() << std::endl;
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                LogPrint("websockets", " storing in wsaddrman inbound pfrom->addr=%s\n", pfrom->addr.ToStringIPPort());
                wsaddrman.Add(addrFrom, addrFrom);
                wsaddrman.Good(addrFrom);
            }
            // can't add here remote wsaddr for the node as we dont know the websocket port (as it is not contained in the version message and we have not extended it)
        }

        LogPrint("websockets", "websockets version received: from addr=%s peer=%d\n", pfrom->addr.ToStringIPPort(), pfrom->id);
        pfrom->fSuccessfullyConnected = true;
        return true;
    }

    // websocket nodes propagation
    else if (strCommand == "wsaddr")
    {
        std::vector<CAddress> vAddr;
        vRecv >> vAddr;

        LogPrint("websockets", "cmd \"wsaddr\" received, vWsaddr.size=%d from peer=%d\n", vAddr.size(), pfrom->id);

        // Don't want wsaddr from older versions
        if (pfrom->nVersion < WSADDR_VERSION)   {
            LogPrintf("version too old for wsaddr %d peer %d", pfrom->nVersion, pfrom->GetId());
            return true;
        }

        if (wsaddrman.size() > 1000)    {
            LogPrintf("websockets addrman full, don't accept wsaddr from peer %d", pfrom->GetId());
            return true;
        }

        if (vAddr.size() > 1000)     {
            LogPrintf("websockets node misbehaving, message wsaddr size() = %u peer %d", vAddr.size(), pfrom->GetId());
            Misbehaving(pfrom->GetId(), 20);
            return true;
        }

        // Store the new addresses
        std::vector<CAddress> vAddrOk;
        int64_t nNow = GetTime();
        int64_t nSince = nNow - 10 * 60;
        BOOST_FOREACH(CAddress& addr, vAddr)
        {
            boost::this_thread::interruption_point();

            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vWsNodes);
                    // Use deterministic randomness to send to the same ws nodes for 24 hours
                    // at a time so the wsaddrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt.IsNull())
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = ArithToUint256(UintToArith256(hashSalt) ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60)));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    std::multimap<uint256, CWsNodePtr> mapMix;
                    for(auto const & pnode : vWsNodes)
                    {
                        if (pnode->nVersion >= WSADDR_VERSION && (pnode->nServices & NODE_WEBSOCKETS) != 0)  // should be relayed to any node supporting "wsaddr"
                        {
                            unsigned int nPointer;
                            memcpy(&nPointer, &pnode, sizeof(nPointer));
                            uint256 hashKey = ArithToUint256(UintToArith256(hashRand) ^ nPointer);
                            hashKey = Hash(BEGIN(hashKey), END(hashKey));
                            mapMix.insert(std::make_pair(hashKey, pnode));
                        }
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (std::multimap<uint256, CWsNodePtr>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi) {
                        LogPrint("websockets"," relaying ws address=%s to peer=%d\n",  addr.ToStringIPPort(), ((*mi).second)->GetId());
                        ((*mi).second)->PushAddress(addr);
                    }
                }
            }
            // Do not store addresses outside our network
            if (fReachable)  {
                vAddrOk.push_back(addr);
                LogPrint("websockets", "cmd \"wsaddr\": adding to wsaddrman addr=%s from peer=%d\n", addr.ToString(), pfrom->id);
            }
            else
                LogPrint("websockets", "cmd \"wsaddr\": not reachable addr=%s from peer=%d\n", addr.ToString(), pfrom->id);
        }
        wsaddrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);

        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        return true;
    }

    else if ((strCommand == "getwsaddr") && (pfrom->fInbound))  // allow to getwsaddr for clients and newly connected nodes to initialize their addrman
    {
        // comment to allow multiply requests for nspv clients:
        // -----
        // Only send one getaddr response per connection to reduce resource waste
        //  and discourage addr stamping of INV announcements.
        //if (pfrom->fSentAddr) {  
        //    LogPrint("net", "Ignoring repeated \"getwsaddr\". peer=%d\n", pfrom->id);
        //    return true;
        //}
        //pfrom->fSentAddr = true;
        //pfrom->sentAddrTime = GetTime();
        // ------
        
        pfrom->vAddrToSend.clear();
        std::vector<CAddress> vWsaddr = wsaddrman.GetAddrAtMost();
        LogPrint("websockets", "received getwsaddr cmd, found vAddr.size=%s from peer=%d\n", vWsaddr.size(), pfrom->GetId());
        BOOST_FOREACH(const CAddress &addr, vWsaddr) {
            LogPrint("websockets", "in response to getwsaddr pushing wsaddr=%s\n", addr.ToString());
            pfrom->PushAddress(addr);
        }
        return true;
    }

    return false;
}

// create periodical messages
bool SendWsMessages(CWsNode *pto, bool fTrickle)
{

    if (pto->nVersion == 0)
        return true;
        
    // Address refresh broadcast
    static int64_t nLastRebroadcast;
    //MilliSleep(1000);
    int64_t nCurrentTime = GetTime();
    if (nCurrentTime - nLastRebroadcast > 60) // check every 60 sec
    {
        // for the node clear known addresses each 24h and advertize local ws listener address
        LOCK(cs_vWsNodes);
        for(auto const pnode : vWsNodes)
        {
            if (nCurrentTime - pnode->nLastRebroadcast > 24 * 60 * 60)   {  // rebroadcast to each node each 24h
                pnode->addrKnown.reset();
                AdvertizeLocalWebSockets(pnode.get());
                pnode->nLastRebroadcast = nCurrentTime;
            }
        }
        nLastRebroadcast = nCurrentTime;
    }

    if (fTrickle)
    {
        // if there are addresses to send for this node send them
        std::vector<CAddress> vAddr;
        vAddr.reserve(pto->vAddrToSend.size());
        for(const CAddress& addr : pto->vAddrToSend)
        {
            if (!pto->addrKnown.contains(addr.GetKey()))
            {
                pto->AddAddressKnown(addr);
                vAddr.push_back(addr);
                LogPrint("websockets", "sending message with ws address=%s to peer=%d\n", addr.ToStringIPPort(), pto->GetId());
                // receiver rejects addr messages larger than 1000
                if (vAddr.size() >= 1000)
                {
                    pto->PushMessage("wsaddr", vAddr);
                    LogPrint("websockets", "sent %d websocket addresses to peer %d\n", vAddr.size(), pto->id);
                    vAddr.clear();
                }
            }
        }
        pto->vAddrToSend.clear();
        if (!vAddr.empty())   {
            pto->PushMessage("wsaddr", vAddr);
            LogPrint("websockets", "sent %d websocket addresses to peer %d\n", vAddr.size(), pto->id);
        }
    }

    return true;                       
}


// requires LOCK(cs_vSend)
void WebSocketSendData(CWsEndpointWrapper *pEndPoint, websocketpp::connection_hdl hdl, CWsNode *pnode)
{
    std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();
    pnode->closeErrorOnSend = 0;

    while (it != pnode->vSendMsg.end()) {
        const CSerializeData &data = *it;
        assert(data.size() > pnode->nSendOffset);
        websocketpp::lib::error_code ec;
        int nBytes = data.size() - pnode->nSendOffset;
        pEndPoint->send(hdl, &data[pnode->nSendOffset], nBytes, websocketpp::frame::opcode::binary, ec);  // should not throw ws exception as ec is passed

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
                LogPrint("websockets", "websocket send error %d %s\n", ec.value(), ec.category().name());
                pnode->closeErrorOnSend = websocketpp::close::status::try_again_later;
                pnode->fDisconnect;
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

void HandleWebSocketMessage(CWsEndpointWrapper *pEndPoint, CWsNode *pNode, websocketpp::connection_hdl hdl, wsserver::message_ptr msg)
{
    pNode->closeErrorOnReceive = 0;

    LOCK(pNode->cs_vRecvMsg);
    if (pNode->ReceiveMsgBytes(msg->get_payload().c_str(), msg->get_payload().size())) {
        pNode->nLastRecv = GetTime(); // needed to prevent inactivity disconnect
        pNode->nRecvBytes += msg->get_payload().size();
        pNode->RecordBytesRecv(msg->get_payload().size());
        if (ProcessMessages(pNode)) {
            LOCK(pNode->cs_vSend); 
            WebSocketSendData(pEndPoint, hdl, pNode);
        }
    }
    else {
        LogPrint("websockets", "error websocket message processing, disconnecting peer %d\n", pNode->id);
        pNode->closeErrorOnReceive = websocketpp::close::status::unsupported_data;
        pNode->fDisconnect = true;
    }
}


class CWebSocketServer : public CWsEndpointWrapper {
public:
    bool init() {

        LogPrintf("Starting websockets listener\n");

        try {

            // Set logging settings
            m_endpoint.set_error_channels(websocketpp::log::elevel::rerror); // & ~websocketpp::log::elevel::devel);
            m_endpoint.set_access_channels(websocketpp::log::alevel::none);

            // Initialize Asio
            m_endpoint.init_asio();
            m_endpoint.set_reuse_addr(true);  // this would prevent komodod restart failure on the socket in use 
                                              // bcz the ws socket might be in CLOSE_WAIT state for a long time komodod has finished (especially if local app has not close the socket), 
                                              // however this is not recommended as allows for other apps to  use this socket as shared (security issue)
                                              // looks like still a websocketpp issue with socket shutdown 
            // Set the default message handler to the echo handler
            m_endpoint.set_message_handler(std::bind(
                &CWebSocketServer::on_message, this,
                std::placeholders::_1, std::placeholders::_2
            ));

            m_endpoint.set_tls_init_handler(bind(&CWebSocketServer::on_tls_init, this, MOZILLA_MODERN, _1)); // add tls init
            m_endpoint.set_open_handler(bind(&CWebSocketServer::on_open, this, _1));
            m_endpoint.set_close_handler(bind(&CWebSocketServer::on_close, this, _1));
            m_endpoint.set_validate_handler(bind(&CWebSocketServer::on_validate, this, _1));
            m_endpoint.set_fail_handler(bind(&CWebSocketServer::on_fail, this, _1));

        } 
        catch (websocketpp::exception const & e) {
            LogPrintf("websockets init failed: %s (for possible a reason check openssl lib 1.1.1 is installed)\n", e.what());
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
                vWsThreads.push_back(websocketpp::lib::make_shared<websocketpp::lib::thread>(&wsserver::run, &m_endpoint));
        } 
        catch (websocketpp::exception const & e) {
            LogPrintf("websockets listener run failed: %s \n", e.what());
            return false;
        }
        return true;
    }

    void on_message(websocketpp::connection_hdl hdl, wsserver::message_ptr msg) {
        boost::this_thread::interruption_point();
        // write a new message
        //std::cerr << __func__ << " payload=" << HexStr(msg->get_payload()) << std::endl;
        //m_endpoint.send(hdl, msg->get_payload(), msg->get_opcode());
        CAddress clientAddr = GetClientAddressFromHdl(hdl);
        LOCK(cs_vWsNodes);
        CWsNodePtr pNode = FindWsNode(clientAddr);
        //CWsNode *pNode = m_connections[hdl];
        if (!pNode) {
            return;
        }
        if (pNode->fDisconnect) {
            return;
        }

        //std::cerr << __func__ << " pnode found=" << clientAddr.ToStringIPPort() << " id=" << pNode->id << std::endl;

        HandleWebSocketMessage(this, pNode.get(), hdl, msg);       
    }

    void stop()
    {
        LogPrintf("Stopping websocket listener...\n");
        websocketpp::lib::error_code ec;
        m_endpoint.stop_listening(ec);

        //LogPrintf("closing websocket connections...\n");
        std::vector<CWsNodePtr> vWsNodesCopy;
        {
            LOCK(cs_vWsNodes);
            vWsNodesCopy = vWsNodes;
        }
        
        for(const auto & pnode : vWsNodesCopy)  
        {
            if (pnode->fInbound)
            {
                //wsserver::connection_ptr conn_ptr = m_endpoint.get_con_from_hdl(conn.first);            
                try {
                    pnode->m_spWsEndpoint->close(pnode->m_hdl, websocketpp::close::status::going_away);
                } catch (websocketpp::exception const & e) { // might be already close from remote site or on a error
                    std::cout << __func__ << " websocketpp::exception: " << e.what() << " (could be okay)" << std::endl;
                }
            }
        }
    
        LogPrintf("Waiting for websocket threads to stop... \n(Note that a possible following 'asio async_shutdown error' message is an expected behaviour)\n"); 
        // expalined here https://github.com/zaphoyd/websocketpp/issues/556
        // see also a possible workaround https://github.com/zaphoyd/websocketpp/issues/545
        // and http://docs.websocketpp.org/faq.html
        /*for (size_t i = 0; i < vWsThreads.size(); i++) {
            vWsThreads[i]->interrupt();
        }*/
        for (size_t i = 0; i < vWsThreads.size(); i++) {
            vWsThreads[i]->join();
        }
        //m_endpoint.close();
        //LOCK(cs_vWsNodes);
        //for (auto const &pNode : vWsNodes)
        //    RemoveWsNode(pNode);
        std::cerr << __func__ << " waiting for listener endpoint stopped state..." << std::endl;
        while(!m_endpoint.stopped())
            MilliSleep(250);

        LogPrintf("Websocket listener stopped\n");
    }

    virtual void send(websocketpp::connection_hdl hdl, void const * payload, size_t len, websocketpp::frame::opcode::value op, websocketpp::lib::error_code & ec) {
        m_endpoint.send(hdl, payload, len, op, ec);
    }

private:
    bool on_validate(websocketpp::connection_hdl hdl)
    {
        return !fWebSocketsInWarmup;
    }

    std::string get_password() {
        return "test";
    }

    context_ptr on_tls_init(tls_mode mode, websocketpp::connection_hdl hdl) {
        namespace asio = websocketpp::lib::asio;

        std::cout << "on_tls_init called with hdl: " << hdl.lock().get() << std::endl;
        std::cout << "using TLS mode: " << (mode == MOZILLA_MODERN ? "Mozilla Modern" : "Mozilla Intermediate") << std::endl;

        context_ptr ctx = websocketpp::lib::make_shared<asio::ssl::context>(asio::ssl::context::sslv23);

        try {
            if (mode == MOZILLA_MODERN) {
                // Modern disables TLSv1
                ctx->set_options(asio::ssl::context::default_workarounds |
                                asio::ssl::context::no_sslv2 |
                                asio::ssl::context::no_sslv3 |
                                asio::ssl::context::no_tlsv1 |
                                asio::ssl::context::single_dh_use);
            } else {
                ctx->set_options(asio::ssl::context::default_workarounds |
                                asio::ssl::context::no_sslv2 |
                                asio::ssl::context::no_sslv3 |
                                asio::ssl::context::single_dh_use);
            }
            ctx->set_password_callback(bind(&CWebSocketServer::get_password, this));

            // self signed test certificate generated with:
            // openssl req -newkey rsa:2048 -nodes -subj '/CN=komodo.test'  -keyout key.pem -x509 -days 365 -out certificate.crt
            // cat certificate.crt key.pem > server.pem
            ctx->use_certificate_chain_file("server.pem");
            ctx->use_private_key_file("server.pem", asio::ssl::context::pem);  
            
            // Example method of generating this file:
            // `openssl dhparam -out dh.pem 2048`
            // Mozilla Intermediate suggests 1024 as the minimum size to use
            // Mozilla Modern suggests 2048 as the minimum size to use.
            ctx->use_tmp_dh_file("dh.pem");
            
            std::string ciphers;
            
            if (mode == MOZILLA_MODERN) {
                ciphers = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!3DES:!MD5:!PSK";
            } else {
                ciphers = "ECDHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:kEDH+AESGCM:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES256-SHA:DHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA:DHE-RSA-AES256-SHA:AES128-GCM-SHA256:AES256-GCM-SHA384:AES128-SHA256:AES256-SHA256:AES128-SHA:AES256-SHA:AES:CAMELLIA:DES-CBC3-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA";
            }
            
            if (SSL_CTX_set_cipher_list(ctx->native_handle() , ciphers.c_str()) != 1) {
                std::cout << "Error setting cipher list" << std::endl;
            }
        } catch (std::exception& e) {
            std::cout << "Exception: " << e.what() << std::endl;
        }
        return ctx;
    }

    void on_fail(websocketpp::connection_hdl hdl) {
        wsserver::connection_ptr con = m_endpoint.get_con_from_hdl(hdl);
        
        //std::cout << "Websocket server error for remote peer " << GetClientAddressFromHdl(hdl).ToStringIPPort() << ": " << con->get_ec() << " - " << con->get_ec().message() << std::endl;
        /*std::cout << con->get_state() << std::endl;
        std::cout << con->get_local_close_code() << std::endl;
        std::cout << con->get_local_close_reason() << std::endl;
        std::cout << con->get_remote_close_code() << std::endl;
        std::cout << con->get_remote_close_reason() << std::endl;
        std::cout << con->get_ec() << " - " << con->get_ec().message() << std::endl;*/
        CAddress clientAddr = GetClientAddressFromHdl(hdl);
        LOCK(cs_vWsNodes);
        CWsNodePtr pNode = FindWsNode(clientAddr);
        if (!pNode) {
            return;
        }

        LogPrint("websockets", "error on inbound connection from ws peer %d\n", pNode->GetId());
        pNode->fDisconnect = true;
        // RemoveWsNode(pNode);  // TODO: make remove the same way like in CWebSocketOutbound, maybe better if nodes are removed in the common loop in net.cpp
        // NOTE: do not modify the pNode might be deleted in RemoveWsNode
    }

    void on_open(websocketpp::connection_hdl hdl)
    {
        CAddress addr = GetClientAddressFromHdl(hdl);

        CWsNodePtr pNode( new CWsNode(INVALID_SOCKET, addr, "", true) );  // not used hSocket for websockets
        pNode->m_spWsEndpoint = spWebSocketServer;
        pNode->m_hdl = hdl;
        {
            LOCK(cs_vWsNodes);
            vWsNodes.push_back(pNode);
        }
    }

    void on_close(websocketpp::connection_hdl hdl)
    {
        CAddress clientAddr = GetClientAddressFromHdl(hdl);
        LOCK(cs_vWsNodes);
        CWsNodePtr pNode = FindWsNode(clientAddr);
        if (!pNode) {
            return;
        }

        LogPrint("websockets", "closing inbound connection from ws peer %d\n", pNode->GetId());

        pNode->fDisconnect = true;
        RemoveWsNode(pNode);  
    }

    virtual void close(websocketpp::connection_hdl hdl, websocketpp::close::status::value status)
    {
        LogPrint("websockets", "closing connection %s\n", GetClientAddressFromHdl(hdl).ToStringIPPort().c_str());
        m_endpoint.close(hdl, status, "");
    }

    virtual void sendWsData(CWsNode *pNode)
    {
        LOCK(pNode->cs_vSend); 
        WebSocketSendData(this, pNode->m_hdl, pNode);
    }

    CAddress GetClientAddressFromHdl(websocketpp::connection_hdl hdl) 
    {
        wsserver::connection_ptr conn_ptr = m_endpoint.get_con_from_hdl(hdl);
        std::string sAddr = conn_ptr->get_remote_endpoint();
        CService svc(sAddr);
        return CAddress(svc);
    }
private:
    wsserver m_endpoint;
};

// object to try peer websocket nodes
class CWebSocketOutbound : public CWsEndpointWrapper {
public:

    CWebSocketOutbound () {
        m_endpoint.set_error_channels(websocketpp::log::elevel::rerror); 
        m_endpoint.set_access_channels(websocketpp::log::alevel::none);

        // Initialize ASIO
        m_endpoint.init_asio();

        // Register our handlers
        m_endpoint.set_socket_init_handler(bind(&CWebSocketOutbound::on_socket_init,this,::_1));
        //m_endpoint.set_http_init_handler(bind(&CWebSocketOutbound::on_socket_init,this,::_1));
        //m_endpoint.set_tls_init_handler(bind(&CWebSocketOutbound::on_socket_init,this,::_1));

        m_endpoint.set_message_handler(bind(&CWebSocketOutbound::on_message,this,::_1,::_2));
        m_endpoint.set_open_handler(bind(&CWebSocketOutbound::on_open,this,::_1));
        m_endpoint.set_close_handler(bind(&CWebSocketOutbound::on_close,this,::_1));
        m_endpoint.set_fail_handler(bind(&CWebSocketOutbound::on_fail,this,::_1));

        m_bFailed = false;
    }

    ~CWebSocketOutbound() {
        // do not delete m_pNode, it is deleted in NetCleanUp
    }

    bool start(CAddress addrConnect, std::string uri) 
    {
        if (fWebSocketsInWarmup)
            return false;
        
        websocketpp::lib::error_code ec;

        m_addrConnect = addrConnect;
        if (!uri.empty())
            m_uri = uri;
        else
            m_uri = "ws://" + addrConnect.ToStringIPPort();

        wsclient::connection_ptr con = m_endpoint.get_connection(m_uri, ec);

        if (ec) {
            m_endpoint.get_alog().write(websocketpp::log::alevel::app, ec.message());
            return false;
        }
       

        m_endpoint.connect(con);       
        return true;
    }

    void run()
    {
        LOCK(cs);
        m_thread = websocketpp::lib::make_shared<websocketpp::lib::thread>(&wsclient::run, &m_endpoint);
    }

    void on_socket_init(websocketpp::connection_hdl hdl) {
    }

    void on_fail(websocketpp::connection_hdl hdl) {
        //wsclient::connection_ptr conn_ptr = m_endpoint.get_con_from_hdl(hdl);
        m_bFailed = true;
        if (m_pNode) { 
            LOCK(cs_vWsNodes);
            LogPrint("websockets", "error on outbound connection to websocket peer %d\n", m_pNode->GetId());

            m_pNode->fDisconnect = true; // mark for disconnection
            RemoveWsNode(m_pNode);  
        }
        else {
            LogPrint("websockets", "could not connect to remote websocket peer %s\n", m_uri.c_str());
        }
    }

    void on_open(websocketpp::connection_hdl hdl) {

        // Add node
        {
            LOCK2(cs, cs_vOutboundEndpoints);
            m_pNode.reset( new CWsNode(INVALID_SOCKET, m_addrConnect, (!m_uri.empty() ? m_uri : ""), false) );
            m_pNode->m_spWsEndpoint = *(std::find_if(vOutboundEndpoints.begin(), vOutboundEndpoints.end(), [&](const ws_endpoint_ptr& sp){ return sp.get() == static_cast<CWsEndpointWrapper*>(this); }));
        }

        m_pNode->m_hdl = hdl;
        m_pNode->nTimeConnected = GetTime();  
        m_pNode->fNetworkNode = true;
        
        {
            LOCK(cs_vWsNodes);
            vWsNodes.push_back(m_pNode);
        }

        m_pNode->PushWsVersion();
        {
            LOCK(m_pNode->cs_vSend); 
            WebSocketSendData(this, hdl, m_pNode.get());
        }
    }
    void on_message(websocketpp::connection_hdl hdl, wsclient::message_ptr msg) {
        HandleWebSocketMessage(this, m_pNode.get(), hdl, msg);
    }
    void on_close(websocketpp::connection_hdl) {
        if ((bool)m_pNode) { 
            LOCK(cs_vWsNodes);
            LogPrint("websockets", "closing outbound connection to ws peer %d\n", m_pNode->GetId());

            m_pNode->fDisconnect = true;
            RemoveWsNode(m_pNode);  
        }
    }

    virtual void close(websocketpp::connection_hdl hdl, websocketpp::close::status::value status)
    {    
        if (m_endpoint.get_con_from_hdl(hdl) != nullptr)    {
            m_endpoint.close(hdl, status, std::string());
        }
    }

    virtual void send(websocketpp::connection_hdl hdl, void const * payload,  size_t len, websocketpp::frame::opcode::value op, websocketpp::lib::error_code & ec) {
        m_endpoint.send(hdl, payload, len, op, ec);
    }

    virtual void sendWsData(CWsNode*)
    {
        if (m_pNode)    {
            LOCK(m_pNode->cs_vSend); 
            WebSocketSendData(this, m_pNode->m_hdl, m_pNode.get());
        }
    }

private:
    wsclient m_endpoint;
    std::string m_uri;
    CAddress m_addrConnect;

public:
    CWsNodePtr m_pNode;
    ws_thread_ptr m_thread;
    bool m_bFailed;
    CCriticalSection cs;
};

CWebSocketOutbound* ConnectWsNode(CAddress addrConnect, const char *pszDest)
{
    if (pszDest == NULL) {
        if (IsLocal(addrConnect))
            return NULL;

        // Look for an existing connection
        CWsNodePtr pnode = FindWsNode((CService)addrConnect);
        if ((bool)pnode)
        {
            return NULL;
        }
    }

    /// debug print
    LogPrint("websockets", "trying websockets connection %s lastseen=%.1fhrs\n",
        pszDest ? pszDest : addrConnect.ToString(),
        pszDest ? 0.0 : (double)(GetTime() - addrConnect.nTime)/3600.0);

    // Connect
    bool proxyConnectionFailed = false;

    CWebSocketOutbound *pwsOutbound = new CWebSocketOutbound();
    bool bStarted;
    try {
        bStarted = pwsOutbound->start(addrConnect, pszDest ? pszDest : "");
    } 
    catch (websocketpp::exception const & e) {
        LogPrint("websockets", "could not start connection to websockets peer %s %s\n", addrConnect.ToStringIPPort().c_str(), pszDest ? pszDest : "");
        delete pwsOutbound;
        return NULL;
    }
    if (bStarted)   {
        wsaddrman.Attempt(addrConnect);
    } else if (!proxyConnectionFailed) {
        // If connecting to the node failed, and failure is not caused by a problem connecting to
        // the proxy, mark this as an attempt.
        wsaddrman.Attempt(addrConnect);
    }

    return pwsOutbound;
}

// if successful, this moves the passed grant to the constructed node
bool OpenWebSocketNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant *grantOutbound /*, const char *pszDest, bool fOneShot*/)
{
    //
    // Initiate outbound network connection
    //
    boost::this_thread::interruption_point();
    //if (!pszDest) {
    if (IsLocal(addrConnect) ||
        FindWsNode((CNetAddr)addrConnect) || CWsNode::IsBanned(addrConnect) ||
        FindWsNode(addrConnect.ToStringIPPort()))
        return false;
    //} else if (FindWsNode(std::string(pszDest)))
    //    return false;

    CWebSocketOutbound * pOutbound = ConnectWsNode(addrConnect, NULL /*pszDest*/);  // Note: pOutbound ptr is stored in pNode (which itself is stored in vWsNodes) and handled from there
    boost::this_thread::interruption_point();

    if (!pOutbound)
        return false;

    // Start the ASIO io_service run loop
    pOutbound->run();

    {
        LOCK(cs_vOutboundEndpoints);
        vOutboundEndpoints.push_back(ws_endpoint_ptr(pOutbound));
    }
    //if (fOneShot)
    //    pnode->fOneShot = true;  // one shot not implemented

    return true;
}

// select a node and create messages to send
void ThreadWebSocketMessageHandler()
{
    boost::mutex condition_mutex;
    boost::unique_lock<boost::mutex> lock(condition_mutex);

    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (true)
    {
        boost::this_thread::interruption_point();

        std::vector<CWsNodePtr> vWsNodesCopy;
        {
            LOCK(cs_vWsNodes);
            vWsNodesCopy = vWsNodes;
        }

        CWsNodePtr pnodeTrickle = NULL;
        if (!vWsNodesCopy.empty())
            pnodeTrickle = vWsNodesCopy[GetRand(vWsNodesCopy.size())];

        bool fSleep = true;

        for(auto const & pnode : vWsNodesCopy)
        {
            if (pnode->fDisconnect)
                continue;

            // Send messages
            {
                TRY_LOCK(pnode->cs_vSend, lockSend);
                if (lockSend)   {
                    bool fTrickle = pnode == pnodeTrickle || pnode->fWhitelisted;
                    SendWsMessages(pnode.get(), fTrickle);
                    if (fTrickle) {
                        WebSocketSendData(pnode->m_spWsEndpoint.get(), pnode->m_hdl, pnode.get());  
                        if (pnode->closeErrorOnSend || pnode->closeErrorOnReceive) {
                            try {
                               pnode->m_spWsEndpoint->close(pnode->m_hdl, (pnode->closeErrorOnSend ? pnode->closeErrorOnSend : pnode->closeErrorOnReceive));              
                            } catch (websocketpp::exception const & e) { // might be already closed from remote site or on a error
                                std::cout << __func__ << " close websocketpp::exception: " << e.what() << " (could be okay)" << std::endl;
                            }
                        }
                    }
                }
            }

            boost::this_thread::interruption_point();
        }

        if (fSleep)
            wsMessageHandlerCondition.timed_wait(lock, boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(100));
    }
}

// join for disconnected outbound threads to stop
static void ThreadWebSocketWaitForDisconnectedThreads()
{
    boost::mutex condition_mutex;
    boost::unique_lock<boost::mutex> lock(condition_mutex);

    SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
    while (true)
    {
        boost::this_thread::interruption_point();

        // remove failed outbound endpoints 
        std::vector<ws_endpoint_ptr> vOutboundEndpointsCopy;
        {
            LOCK(cs_vOutboundEndpoints);
            vOutboundEndpointsCopy = vOutboundEndpoints;
        }
        for (auto &spOutbound : vOutboundEndpointsCopy)
        {
            CWebSocketOutbound *pOutbound = static_cast<CWebSocketOutbound*>(spOutbound.get());
            bool bJoin = false;
            bool bRemove = false;
            {
                LOCK(pOutbound->cs);
                bJoin = (bool)pOutbound->m_bFailed && !pOutbound->m_pNode && (bool)pOutbound->m_thread || (bool)pOutbound->m_thread && fWebSocketsStopping; // join outbound threads that never connected
                // stop tracking either failed outbounds with no node created or 
                bRemove = (bool)pOutbound->m_pNode || pOutbound->m_bFailed || fWebSocketsStopping;
            } 
            if (bRemove) 
            {
                {
                    LOCK(pOutbound->cs);
                    if (bJoin && (bool)pOutbound->m_thread && pOutbound->m_thread->joinable())  {
                        pOutbound->m_thread->join();  
                    }
                }
                {
                    LOCK(cs_vOutboundEndpoints);
                    auto it = std::remove(vOutboundEndpoints.begin(), vOutboundEndpoints.end(), spOutbound);
                    vOutboundEndpoints.erase(it, vOutboundEndpoints.end());
                }
            }
        }
        
        std::vector<CWsNodePtr> vWsNodesCopy;
        {
            LOCK(cs_vWsNodes);
            vWsNodesCopy = vWsNodes;
        }
        for(auto const & pnode : vWsNodesCopy)
        {
            //
            // Inactivity checking
            //
            int64_t nTime = GetTime();
            if (nTime - pnode->nTimeConnected > 60)
            {
                if (pnode->nLastRecv == 0 || pnode->nLastSend == 0)
                {
                    LogPrint("websockets", "socket no message in first 60 seconds, %d %d from %d\n", pnode->nLastRecv != 0, pnode->nLastSend != 0, pnode->id);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastSend > WEBSOCKETS_TIMEOUT_INTERVAL)
                {
                    LogPrintf("websocket sending timeout: %is\n", nTime - pnode->nLastSend);
                    pnode->fDisconnect = true;
                }
                else if (nTime - pnode->nLastRecv > (pnode->nVersion > BIP0031_VERSION ? WEBSOCKETS_TIMEOUT_INTERVAL : 90*60))
                {
                    LogPrintf("websocket receive timeout: %is\n", nTime - pnode->nLastRecv);
                    pnode->fDisconnect = true;
                }
                else if (pnode->nPingNonceSent && pnode->nPingUsecStart + WEBSOCKETS_TIMEOUT_INTERVAL * 1000000 < GetTimeMicros())
                {
                    LogPrintf("websocket ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
                    pnode->fDisconnect = true;
                }

                if (pnode->fDisconnect)
                {   
                    try {
                        pnode->m_spWsEndpoint->close(pnode->m_hdl, websocketpp::close::status::no_status);
                    } catch (websocketpp::exception const & e) { // might be already close from remote site or on a error
                        std::cout << __func__ << " close websocketpp::exception: " << e.what() << " (could be okay)" << std::endl;
                        LOCK(cs_vWsNodes);
                        RemoveWsNode(pnode); 
                    }
                }
            }
        }
        
        // make a copy in order to not to block while waiting for thread join
        std::set<CWsNodePtr> vWsNodesDisconnectedCopy;
        {   
            LOCK(cs_vWsNodesDisconnected);
            vWsNodesDisconnectedCopy = vWsNodesDisconnected;
            vWsNodesDisconnected.clear();

        }

        // join and delete
        for(auto itnode = vWsNodesDisconnectedCopy.begin() ; itnode != vWsNodesDisconnectedCopy.end(); ) 
        {
            if (!(*itnode)->fInbound)
            {
                ws_thread_ptr outboundThreadPtr = static_cast<CWebSocketOutbound*>((*itnode)->m_spWsEndpoint.get())->m_thread;
                if ((bool)outboundThreadPtr && outboundThreadPtr->joinable())
                    outboundThreadPtr->join();  // wait for outbound thread to stop

            }
            auto itnext = vWsNodesDisconnectedCopy.erase(itnode);
            itnode = itnext;

        }
        
        MilliSleep(1000);
    }
}

static int GetOutboundNodes()
{
    LOCK(cs_vWsNodes);
    int nOutbounds = 0;
    for (const auto &pNode : vWsNodes)  
        if (!pNode->fInbound)
            nOutbounds ++;
    return nOutbounds;
}

void ThreadOpenWebSocketConnections()
{
    // Connect to specific addresses
    /*if (mapArgs.count("-wsconnect") && mapMultiArgs["-wsconnect"].size() > 0)
    {
        for (int64_t nLoop = 0;; nLoop++)
        {
            ProcessOneShot();
            BOOST_FOREACH(const std::string& strAddr, mapMultiArgs["-wsconnect"])
            {
                CAddress addr;
                OpenNetworkConnection(addr, NULL, strAddr.c_str());
                for (int i = 0; i < 10 && i < nLoop; i++)
                {
                    MilliSleep(500);
                }
            }
            MilliSleep(500);
        }
    }*/

    // Initiate network connections
    int64_t nStart = GetTime();
    while (true)
    {
        //ProcessOneShot();

        MilliSleep(2000);

        CSemaphoreGrant grant;
        boost::this_thread::interruption_point();

        if (fWebSocketsStopping)
            continue;

        // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
        // if (addrman.size() == 0 && (GetTime() - nStart > 60)) {
        /*if (GetTime() - nStart > 60) {
            static bool done = false;
            if (!done) {
                // skip DNS seeds for staked chains.
                if ( is_STAKED(ASSETCHAINS_SYMBOL) == 0 && KOMODO_DEX_P2P == 0 ) {
                    //LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
                    LogPrintf("Adding fixed seed nodes.\n");
                    addrman.Add(convertSeed6(Params().FixedSeeds()), CNetAddr("127.0.0.1"));
                }
                done = true;
            }
        }*/


        //
        // Choose an address to connect to based on most recently seen
        //
        CAddress addrConnect;

        // Only connect out to one peer per network group (/16 for IPv4).
        // Do this here so we don't have to critsect vWsNodes inside mapAddresses critsect.
        int nOutbound = 0;
        std::set<std::vector<unsigned char> > setConnected;
        {
            LOCK(cs_vWsNodes);
            for (auto const & pnode : vWsNodes) {
                if (!pnode->fInbound) {
                    setConnected.insert(pnode->addr.GetGroup(wsaddrman.m_asmap));  
                    nOutbound++;
                }
            }
        }

        if (GetOutboundNodes() >= 8)
            continue;

        int64_t nANow = GetTime();

        int nTries = 0;
        while (true)
        {
            CAddrInfo addr = wsaddrman.Select();

            // if we selected an invalid address, restart
            if (!addr.IsValid() || setConnected.count(addr.GetGroup(wsaddrman.m_asmap)) || IsLocal(addr))
                break;

#ifdef ENABLE_WEBSOCKETS
            // do not connect to client nodes (looks like this should be added for non-websocket version too):
            if ((addr.nServices & NODE_NETWORK) == 0)
                continue;
#endif

            // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
            // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
            // already-connected network ranges, ...) before trying new addrman addresses.
            nTries++;
            if (nTries > 100)
                break;

            if (IsLimited(addr))
                continue;

            // only consider very recently tried nodes after 30 failed attempts
            if (nANow - addr.nLastTry < 600 && nTries < 30)
                continue;

            // no default port for websockets. TODO: set up a websockets default port
            // do not allow non-default ports, unless after 50 invalid addresses selected already
            //if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50)
            //    continue;

            addrConnect = addr;
            break;
        }

        if (addrConnect.IsValid())  {
            OpenWebSocketNetworkConnection(addrConnect, &grant);
        }
    }
}

static void ThreadOpenAddedWebSocketConnections()
{
    {
        LOCK(cs_vAddedWsNodes);
        vAddedWsNodes = mapMultiArgs["-addwsnode"];
    }

    /*if (HaveNameProxy()) {
    // don't use proxy for websocket listeners
    }*/

    for (unsigned int i = 0; true; i++)
    {
        boost::this_thread::interruption_point();

        std::list<std::string> lAddresses(0);
        {
            LOCK(cs_vAddedNodes);
            for(const std::string& strAddNode : vAddedWsNodes)
                lAddresses.push_back(strAddNode);
        }

        std::list<std::vector<CService> > lservAddressesToAdd(0);
        for(const std::string& strAddNode : lAddresses) {
            std::vector<CService> vservNode(0);
            if(Lookup(strAddNode.c_str(), vservNode, GetWebSocketListenPort(), fNameLookup, 0))
            {
                lservAddressesToAdd.push_back(vservNode);
                {
                    LOCK(cs_setservAddNodeWsAddresses);
                    BOOST_FOREACH(const CService& serv, vservNode)
                        setservAddNodeWsAddresses.insert(serv);  // seems not used
                }
            }
        }
        // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
        // (keeping in mind that addnode entries can have many IPs if fNameLookup)
        {
            LOCK(cs_vWsNodes);
            for(auto const & pnode : vWsNodes)
            {
                for (std::list<std::vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
                {
                    BOOST_FOREACH(const CService& addrNode, *(it))
                        if (pnode->addr == addrNode)
                        {
                            it = lservAddressesToAdd.erase(it);
                            if ( it != lservAddressesToAdd.begin() )
                                it--;
                            break;
                        }
                    if (it == lservAddressesToAdd.end())
                        break;
                }
            }
        }
        if (!fWebSocketsStopping)
        {
            for(std::vector<CService>& vserv : lservAddressesToAdd)
            {
                if (GetOutboundNodes() >= 8)
                    break;
                CSemaphoreGrant grant;
                OpenWebSocketNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
                MilliSleep(500);
            }
        }
        MilliSleep(120000); // Retry every 2 minutes
    }
}


// start stop

bool StartWebSockets(boost::thread_group& threadGroup) 
{
    spWebSocketServer.reset(new CWebSocketServer);

    if (!static_cast<CWebSocketServer*>(spWebSocketServer.get())->init())
        return false;
    if (!static_cast<CWebSocketServer*>(spWebSocketServer.get())->run())
        return false;

    //if (semWsOutbound == NULL)
    //    semWsOutbound = new CSemaphore(8); // allow 8 peer connections

    // Initiate outbound connections
    wsThreadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "wsopencon", &ThreadOpenWebSocketConnections));

    // Initiate outbound 'addwsnode' connections
    wsThreadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "wsopenaddedcon", &ThreadOpenAddedWebSocketConnections));

    // periodical send messages thread
    wsThreadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "wsmsghand", &ThreadWebSocketMessageHandler));

    // wait for disconnected oupud threads
    wsThreadGroup.create_thread(boost::bind(&TraceThread<void (*)()>, "wsdiscon", &ThreadWebSocketWaitForDisconnectedThreads));

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
    if (!bWebSocketsStarted) 
        return;

    if (!spWebSocketServer)
        return;

    fWebSocketsStopping = true;
    static_cast<CWebSocketServer*>(spWebSocketServer.get())->stop();

    LogPrintf("Closing outbound websocket connections...\n");
    {
        std::vector<CWsNodePtr> vWsNodesCopy;
        {
            LOCK(cs_vWsNodes);
            vWsNodesCopy = vWsNodes;
        }
        for(const auto &pnode : vWsNodesCopy)  
        {
            if (!pnode->fInbound)  {  // inbounds are close when the ws listener stops
                try {
                    pnode->m_spWsEndpoint->close(pnode->m_hdl, websocketpp::close::status::going_away);
                } catch (websocketpp::exception const & e) { // might be already close from remote site or on a error
                    std::cout << __func__ << " close websocketpp::exception: " << e.what() << " (could be okay)" << std::endl;
                }
            }
        }
    }

    LogPrintf("Waiting for websockets outbound threads to stop...\n"); 
    // joined in "wsdiscon" thread:
    while(vWsNodesDisconnected.size() > 0 || GetOutboundNodes() > 0 || vOutboundEndpoints.size() > 0) {  
        //std::cerr << __func__ << " vWsNodesDisconnected.size()=" << vWsNodesDisconnected.size() << " GetOutboundNodes()=" << GetOutboundNodes() << " vOutboundEndpoints.size()=" << vOutboundEndpoints.size() << std::endl;
        MilliSleep(500);
    }

    LogPrintf("Waiting for websockets work threads to stop...\n");

    wsThreadGroup.interrupt_all();
    wsThreadGroup.join_all();

    LogPrintf("All websockets threads stopped\n");

}

// debug rpc impl
UniValue GetWsPeers()
{
    UniValue result(UniValue::VARR);
    std::vector<CNodeStats> vstats;

    {
        LOCK(cs_vWsNodes);
        vstats.reserve(vWsNodes.size());
        for(const auto & pnode : vWsNodes) {
            CNodeStats stats;
            pnode->copyStats(stats, wsaddrman.m_asmap);
            vstats.push_back(stats);
        }
    }

    for (auto const &stats : vstats)    {
        UniValue peer(UniValue::VOBJ);

        peer.push_back(Pair("id", stats.nodeid));
        peer.push_back(Pair("addr", stats.addrName));
        if (!(stats.addrLocal.empty()))
            peer.push_back(Pair("addrlocal", stats.addrLocal));
        peer.push_back(Pair("services", strprintf("%016x", stats.nServices)));
        peer.push_back(Pair("version", stats.nVersion));
        // Use the sanitized form of subver here, to avoid tricksy remote peers from
        // corrupting or modifying the JSON output by putting special characters in
        // their ver message.
        peer.push_back(Pair("subver", stats.cleanSubVer));
        peer.push_back(Pair("inbound", stats.fInbound));

        result.push_back(peer);
    }
    return result;
}

// temp rpc to dump addrman table. TODO: remove this code
UniValue printaddrman(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 0)
    {
        std::string msg = "printaddrman\n"
            "\nReturns an array of addresses\n";
        throw std::runtime_error(msg);
    }
    std::vector<CAddrInfo> addrInfos = addrman.GetAddrInfoAll();
    UniValue result (UniValue::VOBJ);
    UniValue array (UniValue::VARR);
    int64_t nNow = GetTime();
    for(auto const & a : addrInfos) {
        UniValue e(UniValue::VOBJ);

        e.pushKV("address", a.ToString());
        std::ostringstream strhex;
        strhex << std::hex << a.nServices;
        e.pushKV("services", strhex.str());
        e.pushKV("source", addrman.GetSource(a).ToString());
        e.pushKV("IsTerrible", a.IsTerrible());
        e.pushKV("SinceLastTry", nNow - a.nLastTry);
        e.pushKV("SinceLastSeen", nNow - a.nTime);
        array.push_back(e);
    }
    result.pushKV("addresses", array);

    return result;
}

// temp rpc to dump addrman table. TODO: remove this code
UniValue printwsaddrman(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    if (fHelp || params.size() != 0)
    {
        std::string msg = "printwsaddrman\n"
            "\nReturns an array of addresses\n";
        throw std::runtime_error(msg);
    }

    std::vector<CAddrInfo> addrInfos = wsaddrman.GetAddrInfoAll();
    UniValue result (UniValue::VOBJ);
    UniValue array (UniValue::VARR);
    int64_t nNow = GetTime();
    for(auto const & a : addrInfos) {
        UniValue e(UniValue::VOBJ);

        e.pushKV("wsaddress", a.ToString());
        std::ostringstream strhex;
        strhex << std::hex << a.nServices;
        e.pushKV("services", strhex.str());
        e.pushKV("source", wsaddrman.GetSource(a).ToString());
        e.pushKV("IsTerrible", a.IsTerrible());
        e.pushKV("SinceLastTry", nNow - a.nLastTry);
        e.pushKV("SinceLastSeen", nNow - a.nTime);
        array.push_back(e);
    }
    result.pushKV("wsaddresses", array);

    return result;
}

UniValue getwspeers(const UniValue& params, bool fHelp, const CPubKey& remotepk)
{
    UniValue result = GetWsPeers();
    return result;
}


static const CRPCCommand commands[] =
{ //  category              name                      actor (function)         okSafeMode
  //  --------------------- ------------------------  -----------------------  ----------
    { "hidden",               "printaddrman",          &printaddrman,          true  },
    { "hidden",               "printwsaddrman",          &printwsaddrman,          true  },
    { "hidden",               "getwspeers",          &getwspeers,          true  },
};

void RegisterWebSocketsRPCCommands(CRPCTable &tableRPC)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        tableRPC.appendCommand(commands[vcidx].name, &commands[vcidx]);
}