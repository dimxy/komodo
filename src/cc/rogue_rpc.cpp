
/******************************************************************************
 * Copyright © 2014-2019 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

#include "cJSON.h"

#define ROGUE_REGISTRATION 5
#define ROGUE_REGISTRATIONSIZE (100 * 10000)
#define ROGUE_MAXPLAYERS 64 // need to send unused fees back to globalCC address to prevent leeching
#define ROGUE_MAXKEYSTROKESGAP 60

/*
 Roguelander - using highlander competition between rogue players

 anybody can create a game by specifying maxplayers and buyin. Buyin of 0.00 ROGUE is for newbies and a good way to get experience. All players will start in the identical level, but each level after that will be unique to each player.
 
 There are two different modes that is based on the maxplayers. If maxplayers is one, then it is basically a practice/farming game where there are no time limits. Also, the gold -> ROGUE conversion rate is cut in half. You can start a single player game as soon as the newgame tx is confirmed.
 
 If maxplayers is more than one, then it goes into highlander mode. There can only be one winner. Additionally, multiplayer mode adds a timelimit of ROGUE_MAXKEYSTROKESGAP (60) blocks between keystrokes updates. That is approx. one hour and every level it does an automatic update, so as long as you are actively playing, it wont be a problem. The is ROGUE_REGISTRATION blocks waiting period before any of the players can start. The random seed for the game is based on the future blockhash so that way, nobody is able to start before the others.
 
 rogue is not an easy game to win, so it could be that the winner for a specific group is simply the last one standing. If you bailout of a game you cant win, but you can convert all the ingame gold you gathered at 0.001 ROGUE each. [In the event that there arent enough globally locked funds to payout the ROGUE, you will have to wait until there is. Since 10% of all economic activity creates more globally locked funds, it is just a matter of time before there will be enough funds to payout. Also, you can help speed this up by encouraging others to transact in ROGUE]
 
 Of course, the most direct way to win, is to get the amulet and come back out of the dungeon. The winner will get all of the buyins from all the players in addition to 0.01 ROGUE for every ingame gold.
 
 The above alone is enough to make the timeless classic solitaire game into a captivating multiplayer game, where real coins are at stake. However, there is an even better aspect to ROGUE! Whenever your player survives a game, even when you bailout, the entire player and his pack is preserved on the blockchain and becomes a nonfungible asset that you can trade for ROGUE.
 
 Additionally, instead of just being a collectors item with unique characteristics, the rogue playerdata can be used in any ROGUE game. Just specify the txid that created your character when you register and your character is restored. The better your characteristics your playerdata has, the more likely you are to win in multiplayer mode to win all the buyins. So there is definite economic value to a strong playerdata.
 
 You can farm characters in single player mode to earn ROGUE or to make competitive playerdata sets. You can purchase existing playerdata for collecting, or for using in battle.
 
 Here is how to play:
 
 ./komodo-cli -ac_name=ROGUE cclib newgame 17 \"[3,10]\" -> this will create a hex transaction that when broadcast with sendrawtransaction will get a gametxid onto the blockchain. This specific command was for 3 players and a buyin of 10 ROGUE. Lets assume the gametxid is 4fd6f5cad0fac455e5989ca6eef111b00292845447075a802e9335879146ad5a, most all the other commands will need the gametxid.
 
 you can always find all the existing games with:
 
  ./komodo-cli -ac_name=ROGUE cclib pending 17
 
 and info about a specific game with:
 
  ./komodo-cli -ac_name=ROGUE cclib gameinfo 17 \"[%224fd6f5cad0fac455e5989ca6eef111b00292845447075a802e9335879146ad5a%22]\"

 due to quirks of various parsing at the shell, rpc and internal level, the above convention is used where %22 is added where " should be. also all fields are separated by , without any space.
 
 When you do a gameinfo command it will show a "run" field and that will tell you if you are registered for the game or not. If not, the "run" field shows the register syntax you need to do, if you are registered, it will show the command line to start the rogue game that is playing the registered game.
 
./komodo-cli -ac_name=ROGUE cclib register 17 \"[%224fd6f5cad0fac455e5989ca6eef111b00292845447075a802e9335879146ad5a%22,%22playerdata_txid%22]\"

 If you want to cash in your ingame gold and preserve your character for another battle, do the bailout:
 
./komodo-cli -ac_name=ROGUE cclib bailout 17 \"[%224fd6f5cad0fac455e5989ca6eef111b00292845447075a802e9335879146ad5a%22]\"

 If you won your game before anybody else did or if you are the last one left who didnt bailout, you can claim the prize:
 
 ./komodo-cli -ac_name=ROGUE cclib highlander 17 \"[%224fd6f5cad0fac455e5989ca6eef111b00292845447075a802e9335879146ad5a%22]\"

 The txid you get from the bailout or highlander transactions is the "playerdata_txid" that you can use in future games.
 

 Transaction details
 creategame
 vout0 -> txfee highlander vout TCBOO creation
 vout1 to vout.maxplayers+1 -> 1of2 registration ROGUE_REGISTRATIONSIZE batons
 vout2+maxplayers to vout.2*maxplayers+1 -> 1of2 registration txfee batons for game completion
 
 register
 vin0 -> ROGUE_REGISTRATIONSIZE 1of2 registration baton from creategame
 vin1 -> optional nonfungible character vout @
 vin2 -> original creation TCBOO playerdata used
 vin3+ -> buyin
 vout0 -> keystrokes/completion baton
 
 keystrokes
 vin0 -> txfee 1of2 baton from registration or previous keystrokes
 opret -> user input chars
 
 bailout: must be within ROGUE_MAXKEYSTROKESGAP blocks of last keystrokes
 vin0 -> keystrokes baton of completed game with Q
 vout0 -> 1% ingame gold
 
 highlander
 vin0 -> txfee highlander vout from creategame TCBOO creation
 vin1 -> keystrokes baton of completed game, must be last to quit or first to win, only spent registration batons matter. If more than ROGUE_MAXKEYSTROKESGAP blocks since last keystrokes, it is forfeit
 vins -> rest of unspent registration utxo so all newgame vouts are spent
 vout0 -> nonfungible character with pack @
 vout1 -> 1% ingame gold and all the buyins
 
 
 then to register you need to spend one of the vouts and also provide the buyin
 once you register the gui mode is making automatic keystrokes tx with the raw chars in opreturn.
 if during the registration, you provide a character as an input, your gameplay starts with that character instead of the default
 
 each keystrokes tx spends a baton vout that you had in your register tx
 
 so from the creategame tx, you can trace the maxplayers vouts to find all the registrations and all the keystrokes to get the keyboard events
 
 If you quit out of the game, then the in game gold that you earned can be converted to ROGUE coins, but unless you are the last one remaining, any character you input, is permanently spent
 
 so you can win a multiplayer by being the last player to quit or the first one to win. In either case, you would need to spend a special highlander vout in the original creategame tx. having this as an input allows to create a tx that has the character as the nonfungible token, collect all the buyin and of course the ingame gold
 
 once you have a non-fungible token, ownership of it can be transferred or traded or spent in a game
 */

// todo:
// make register a token burn
// convert playertxid to the original playertxid
// verify keystrokes tx is in mempool and confirmed
// verify amulet possession in pack

//////////////////////// start of CClib interface
//./komodod -ac_name=ROGUE -ac_supply=1000000 -pubkey=03951a6f7967ad784453116bc55cd30c54f91ea8a5b1e9b04d6b29cfd6b395ba6c -addnode=5.9.102.210  -ac_cclib=rogue -ac_perc=10000000 -ac_reward=100000000 -ac_cc=60001 -ac_script=2ea22c80203d1579313abe7d8ea85f48c65ea66fc512c878c0d0e6f6d54036669de940febf8103120c008203000401cc > /dev/null &

// cclib newgame 17 \"[3,10]\"
// cclib pending 17
// ./c cclib gameinfo 17 \"[%22aa81321d8889f881fe3d7c68c905b7447d9143832b0abbef6c2cab49dff8b0cc%22]\"
// cclib register 17 \"[%22aa81321d8889f881fe3d7c68c905b7447d9143832b0abbef6c2cab49dff8b0cc%22]\"
// ./rogue <seed> gui -> creates keystroke files
// ./c cclib register 17 \"[%22aa81321d8889f881fe3d7c68c905b7447d9143832b0abbef6c2cab49dff8b0cc%22,%226c88eb35f1f9eadabb0fb00c5b25b44cc60e99013ec9ce6871acd8ed7541de93%22]\"
// ./c cclib bailout 17 \"[%228fd5b29b77ef90d7b7747c779530a8b005f7c236ea0ddb1fe68a392ea3b6cdf1%22]\"


#define MAXPACK 23
struct rogue_packitem
{
    int32_t type,launch,count,which,hplus,dplus,arm,flags,group;
    char damage[8],hurldmg[8];
};
struct rogue_player
{
    int32_t gold,hitpoints,strength,level,experience,packsize,dungeonlevel,pad;
    struct rogue_packitem roguepack[MAXPACK];
};
int32_t rogue_replay2(uint8_t *newdata,uint64_t seed,char *keystrokes,int32_t num,struct rogue_player *player);
#define ROGUE_DECLARED_PACK
void rogue_packitemstr(char *packitemstr,struct rogue_packitem *item);

CScript rogue_newgameopret(int64_t buyin,int32_t maxplayers)
{
    CScript opret; uint8_t evalcode = EVAL_ROGUE;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << 'G' << buyin << maxplayers);
    return(opret);
}

CScript rogue_registeropret(uint256 gametxid,uint256 playertxid)
{
    CScript opret; uint8_t evalcode = EVAL_ROGUE;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << 'R' << gametxid << playertxid);
    return(opret);
}

CScript rogue_keystrokesopret(uint256 gametxid,uint256 batontxid,CPubKey pk,std::vector<uint8_t>keystrokes)
{
    CScript opret; uint8_t evalcode = EVAL_ROGUE;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << 'K' << gametxid << batontxid << pk << keystrokes);
    return(opret);
}

CScript rogue_highlanderopret(uint8_t funcid,uint256 gametxid,int32_t regslot,CPubKey pk,std::vector<uint8_t>playerdata)
{
    CScript opret; uint8_t evalcode = EVAL_ROGUE;
    opret << OP_RETURN << E_MARSHAL(ss << evalcode << funcid << gametxid << regslot << pk << playerdata);
    return(opret);
}

uint8_t rogue_highlanderopretdecode(uint256 &gametxid,int32_t &regslot,CPubKey &pk,std::vector<uint8_t> &playerdata,CScript scriptPubKey)
{
    std::vector<uint8_t> vopret,vopret2; uint8_t e,f; uint256 tokenid; std::vector<CPubKey> voutPubkeys;
    GetOpReturnData(scriptPubKey,vopret);
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> gametxid; ss >> regslot; ss >> pk; ss >> playerdata) != 0 && e == EVAL_ROGUE && (f == 'H' || f == 'Q') )
        return(f);
    else if ( (f= DecodeTokenOpRet(scriptPubKey,e,tokenid,voutPubkeys,vopret,vopret2)) == 'c' || f == 't' )
    {
        if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> gametxid; ss >> regslot; ss >> pk; ss >> playerdata) != 0 && e == EVAL_ROGUE && (f == 'H' || f == 'Q') )
        {
            return(f);
        }
    }
    return(0);
}

uint8_t rogue_keystrokesopretdecode(uint256 &gametxid,uint256 &batontxid,CPubKey &pk,std::vector<uint8_t> &keystrokes,CScript scriptPubKey)
{
    std::vector<uint8_t> vopret; uint8_t e,f;
    GetOpReturnData(scriptPubKey,vopret);
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> gametxid; ss >> batontxid; ss >> pk; ss >> keystrokes) != 0 && e == EVAL_ROGUE && f == 'K' )
    {
        return(f);
    }
    return(0);
}

uint8_t rogue_registeropretdecode(uint256 &gametxid,uint256 &playertxid,CScript scriptPubKey)
{
    std::vector<uint8_t> vopret; uint8_t e,f;
    GetOpReturnData(scriptPubKey,vopret);
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> gametxid; ss >> playertxid) != 0 && e == EVAL_ROGUE && f == 'R' )
    {
        return(f);
    }
    return(0);
}

uint8_t rogue_newgameopreturndecode(int64_t &buyin,int32_t &maxplayers,CScript scriptPubKey)
{
    std::vector<uint8_t> vopret; uint8_t e,f;
    GetOpReturnData(scriptPubKey,vopret);
    if ( vopret.size() > 2 && E_UNMARSHAL(vopret,ss >> e; ss >> f; ss >> buyin; ss >> maxplayers) != 0 && e == EVAL_ROGUE && f == 'G' )
    {
        return(f);
    }
    return(0);
}

void rogue_univalue(UniValue &result,const char *method,int64_t maxplayers,int64_t buyin)
{
    if ( method != 0 )
    {
        result.push_back(Pair("name","rogue"));
        result.push_back(Pair("method",method));
    }
    if ( maxplayers > 0 )
        result.push_back(Pair("maxplayers",maxplayers));
    if ( buyin >= 0 )
    {
        result.push_back(Pair("buyin",ValueFromAmount(buyin)));
        if ( buyin == 0 )
            result.push_back(Pair("type","newbie"));
        else result.push_back(Pair("type","buyin"));
    }
}

int32_t rogue_iamregistered(int32_t maxplayers,uint256 gametxid,CTransaction tx,char *myrogueaddr)
{
    int32_t i,vout; uint256 spenttxid,hashBlock; CTransaction spenttx; char destaddr[64];
    for (i=0; i<maxplayers; i++)
    {
        destaddr[0] = 0;
        vout = i+1;
        if ( myIsutxo_spent(spenttxid,gametxid,vout) >= 0 )
        {
            if ( GetTransaction(spenttxid,spenttx,hashBlock,false) != 0 && spenttx.vout.size() > 0 )
            {
                Getscriptaddress(destaddr,spenttx.vout[0].scriptPubKey);
                if ( strcmp(myrogueaddr,destaddr) == 0 )
                    return(1);
                //else fprintf(stderr,"myaddr.%s vs %s\n",myrogueaddr,destaddr);
            } //else fprintf(stderr,"cant find spenttxid.%s\n",spenttxid.GetHex().c_str());
        } //else fprintf(stderr,"vout %d is unspent\n",vout);
    }
    return(0);
}

int32_t rogue_playersalive(int32_t &numplayers,uint256 gametxid,int32_t maxplayers)
{
    int32_t i,alive = 0; uint64_t txfee = 10000;
    numplayers = 0;
    for (i=0; i<maxplayers; i++)
    {
        if ( CCgettxout(gametxid,1+i,1) < 0 )
        {
            numplayers++;
            if (CCgettxout(gametxid,1+maxplayers+i,1) == txfee )
                alive++;
        }
    }
    return(alive);
}

uint64_t rogue_gamefields(UniValue &obj,int64_t maxplayers,int64_t buyin,uint256 gametxid,char *myrogueaddr)
{
    CBlockIndex *pindex; int32_t ht,delay,numplayers; uint256 hashBlock; uint64_t seed=0; char cmd[512]; CTransaction tx;
    if ( GetTransaction(gametxid,tx,hashBlock,false) != 0 && (pindex= komodo_blockindex(hashBlock)) != 0 )
    {
        ht = pindex->GetHeight();
        delay = ROGUE_REGISTRATION * (maxplayers > 1);
        obj.push_back(Pair("height",ht));
        obj.push_back(Pair("start",ht+delay));
        if ( komodo_nextheight() > ht+delay )
        {
            if ( (pindex= komodo_chainactive(ht+delay)) != 0 )
            {
                hashBlock = pindex->GetBlockHash();
                obj.push_back(Pair("starthash",hashBlock.ToString()));
                memcpy(&seed,&hashBlock,sizeof(seed));
                seed &= (1LL << 62) - 1;
                obj.push_back(Pair("seed",(int64_t)seed));
                if ( rogue_iamregistered(maxplayers,gametxid,tx,myrogueaddr) > 0 )
                    sprintf(cmd,"cc/rogue/rogue %llu %s",(long long)seed,gametxid.ToString().c_str());
                else sprintf(cmd,"./komodo-cli -ac_name=%s cclib register %d \"[%%22%s%%22]\"",ASSETCHAINS_SYMBOL,EVAL_ROGUE,gametxid.ToString().c_str());
                obj.push_back(Pair("run",cmd));
            }
        }
    }
    obj.push_back(Pair("alive",rogue_playersalive(numplayers,gametxid,maxplayers)));
    obj.push_back(Pair("numplayers",numplayers));
    obj.push_back(Pair("maxplayers",maxplayers));
    obj.push_back(Pair("buyin",ValueFromAmount(buyin)));
    return(seed);
}

int32_t rogue_isvalidgame(struct CCcontract_info *cp,int32_t &gameheight,CTransaction &tx,int64_t &buyin,int32_t &maxplayers,uint256 txid)
{
    uint256 hashBlock; int32_t i,numvouts; char coinaddr[64]; CPubKey roguepk; uint64_t txfee = 10000;
    buyin = maxplayers = 0;
    if ( GetTransaction(txid,tx,hashBlock,false) != 0 && (numvouts= tx.vout.size()) > 1 )
    {
        gameheight = komodo_blockheight(hashBlock);
        if ( IsCClibvout(cp,tx,0,cp->unspendableCCaddr) >= txfee && myIsutxo_spentinmempool(ignoretxid,ignorevin,txid,0) == 0 )
        {
            if ( rogue_newgameopreturndecode(buyin,maxplayers,tx.vout[numvouts-1].scriptPubKey) == 'G' )
            {
                if ( numvouts > maxplayers+1 )
                {
                    for (i=0; i<maxplayers; i++)
                    {
                        if ( tx.vout[i+1].nValue != ROGUE_REGISTRATIONSIZE )
                            break;
                        if ( tx.vout[maxplayers+i+1].nValue != txfee )
                            break;
                    }
                    if ( i == maxplayers )
                        return(0);
                    else return(-5);
                }
                else return(-4);
            } else return(-3);
        } else return(-2);
    } else return(-1);
}

UniValue rogue_playerobj(std::vector<uint8_t> playerdata,uint256 playertxid)
{
    int32_t i; struct rogue_player P; char packitemstr[512],*datastr; UniValue obj(UniValue::VOBJ),a(UniValue::VARR);
    memset(&P,0,sizeof(P));
    if ( playerdata.size() > 0 )
    {
        datastr = (char *)malloc(playerdata.size()*2+1);
        for (i=0; i<playerdata.size(); i++)
        {
            ((uint8_t *)&P)[i] = playerdata[i];
            sprintf(&datastr[i<<1],"%02x",playerdata[i]);
        }
        datastr[i<<1] = 0;
    }
    int32_t gold,hitpoints,strength,level,experience,packsize,dungeonlevel,pad;
    for (i=0; i<P.packsize&&i<MAXPACK; i++)
    {
        rogue_packitemstr(packitemstr,&P.roguepack[i]);
        a.push_back(packitemstr);
    }
    obj.push_back(Pair("playertxid",playertxid.GetHex()));
    obj.push_back(Pair("data",datastr));
    free(datastr);
    obj.push_back(Pair("pack",a));
    obj.push_back(Pair("packsize",(int64_t)P.packsize));
    obj.push_back(Pair("hitpoints",(int64_t)P.hitpoints));
    obj.push_back(Pair("strength",(int64_t)P.strength));
    obj.push_back(Pair("level",(int64_t)P.level));
    obj.push_back(Pair("experience",(int64_t)P.experience));
    obj.push_back(Pair("dungeonlevel",(int64_t)P.dungeonlevel));
    return(obj);
}

int32_t rogue_iterateplayer(uint256 &registertxid,uint256 firsttxid,int32_t firstvout,uint256 lasttxid)     // retrace playertxid vins to reach highlander <- this verifies player is valid and rogue_playerdataspend makes sure it can only be used once
{
    uint256 spenttxid,txid = firsttxid; int32_t spentvini,vout = firstvout;
    registertxid = zeroid;
    if ( vout < 0 )
        return(-1);
    while ( (spentvini= myIsutxo_spent(spenttxid,txid,vout)) == 0 )
    {
        txid = spenttxid;
        vout = spentvini;
        if ( registertxid == zeroid )
            registertxid = txid;
    }
    if ( txid == lasttxid )
        return(0);
    else
    {
        fprintf(stderr,"firsttxid.%s/v%d -> %s != last.%s\n",firsttxid.ToString().c_str(),firstvout,txid.ToString().c_str(),lasttxid.ToString().c_str());
        return(-1);
    }
}

/*
 playertxid is whoever owns the nonfungible satoshi and it might have been bought and sold many times.
 highlander is the game winning tx with the player data and is the only place where the unique player data exists
 origplayergame is the gametxid that ends up being won by the highlander and they are linked directly as the highlander tx spends gametxid.vout0
 */

int32_t rogue_playerdata(struct CCcontract_info *cp,uint256 &origplayergame,CPubKey &pk,std::vector<uint8_t> &playerdata,uint256 playertxid)
{
    uint256 origplayertxid,hashBlock,gametxid,registertxid; CTransaction gametx,playertx,highlandertx; std::vector<uint8_t> vopret; uint8_t *script,e,f; int32_t i,regslot,gameheight,numvouts,maxplayers; int64_t buyin;
    if ( GetTransaction(playertxid,playertx,hashBlock,false) != 0 && (numvouts= playertx.vout.size()) > 0 )
    {
        //GetOpReturnData(playertx.vout[numvouts-1].scriptPubKey,vopret);
        //script = (uint8_t *)vopret.data();
        if ( (f= rogue_highlanderopretdecode(gametxid,regslot,pk,playerdata,playertx.vout[numvouts-1].scriptPubKey)) == 'H' || f == 'Q' )
        {
            fprintf(stderr,"gametxid.%s\n",gametxid.GetHex().c_str());
            //memcpy(&gametxid,script+2,sizeof(gametxid));
            if ( rogue_isvalidgame(cp,gameheight,gametx,buyin,maxplayers,gametxid) == 0 )
            {
                fprintf(stderr,"playertxid.%s got vin.%s/v%d gametxid.%s iterate.%d\n",playertxid.ToString().c_str(),playertx.vin[1].prevout.hash.ToString().c_str(),(int32_t)playertx.vin[1].prevout.n-maxplayers,gametxid.ToString().c_str(),rogue_iterateplayer(registertxid,gametxid,playertx.vin[1].prevout.n-maxplayers,playertxid));
                if ( playertx.vin[1].prevout.hash == gametxid && rogue_iterateplayer(registertxid,gametxid,playertx.vin[1].prevout.n-maxplayers,playertxid) == 0 )
                {
                    // if registertxid has vin from pk, it can be used
                    return(0);
                } else fprintf(stderr,"hash mismatch or illegal gametxid\n");
            }
        }
    }
    return(-1);
}

int32_t rogue_playerdataspend(CMutableTransaction &mtx,uint256 playertxid,uint256 origplayergame)
{
    int64_t txfee = 10000;
    if ( CCgettxout(playertxid,0,1) == 1 ) // not sure if this is enough validation
    {
        mtx.vin.push_back(CTxIn(playertxid,0,CScript()));
        return(0);
    } else return(-1);
}

int32_t rogue_findbaton(struct CCcontract_info *cp,uint256 &playertxid,char **keystrokesp,int32_t &numkeys,int32_t &regslot,std::vector<uint8_t> &playerdata,uint256 &batontxid,int32_t &batonvout,int64_t &batonvalue,int32_t &batonht,uint256 gametxid,CTransaction gametx,int32_t maxplayers,char *destaddr,int32_t &numplayers)
{
    int32_t i,numvouts,spentvini,matches = 0; CPubKey pk; uint256 spenttxid,hashBlock,txid,origplayergame; CTransaction spenttx,matchtx,batontx; std::vector<uint8_t> checkdata; CBlockIndex *pindex; char ccaddr[64],*keystrokes=0;
    numkeys = numplayers = 0;
    playertxid = zeroid;
    for (i=0; i<maxplayers; i++)
    {
        if ( myIsutxo_spent(spenttxid,gametxid,i+1) >= 0 )
        {
            if ( GetTransaction(spenttxid,spenttx,hashBlock,false) != 0 && spenttx.vout.size() > 0 )
            {
                numplayers++;
                Getscriptaddress(ccaddr,spenttx.vout[0].scriptPubKey);
                if ( strcmp(destaddr,ccaddr) == 0 )
                {
                    matches++;
                    regslot = i;
                    matchtx = spenttx;
                } else fprintf(stderr,"%d+1 doesnt match %s vs %s\n",i,ccaddr,destaddr);
            } else fprintf(stderr,"%d+1 couldnt find spenttx.%s\n",i,spenttxid.GetHex().c_str());
        } else fprintf(stderr,"%d+1 unspent\n",i);
    }
    if ( matches == 1 )
    {
        if ( myIsutxo_spent(spenttxid,gametxid,maxplayers+i+1) < 0 )
        {
            numvouts = matchtx.vout.size();
            //fprintf(stderr,"matches.%d numvouts.%d\n",matches,numvouts);
            if ( rogue_registeropretdecode(txid,playertxid,matchtx.vout[numvouts-1].scriptPubKey) == 'R' && txid == gametxid )
            {
                if ( playertxid == zeroid || rogue_playerdata(cp,origplayergame,pk,playerdata,playertxid) == 0 )
                {
                    txid = matchtx.GetHash();
                    //fprintf(stderr,"scan forward playertxid.%s spenttxid.%s\n",playertxid.GetHex().c_str(),txid.GetHex().c_str());
                    while ( CCgettxout(txid,0,1) < 0 )
                    {
                        spenttxid = zeroid;
                        spentvini = -1;
                        if ( (spentvini= myIsutxo_spent(spenttxid,txid,0)) >= 0 )
                            txid = spenttxid;
                        else if ( myIsutxo_spentinmempool(spenttxid,spentvini,txid,0) == 0 || spenttxid == zeroid )
                        {
                            fprintf(stderr,"mempool tracking error %s/v0\n",txid.ToString().c_str());
                            return(-2);
                        }
                        if ( spentvini != 0 )
                            return(-3);
                        if ( keystrokesp != 0 && GetTransaction(spenttxid,spenttx,hashBlock,false) != 0 && spenttx.vout.size() == 2 )
                        {
                            uint256 g,b; CPubKey p; std::vector<uint8_t> k;
                            if ( rogue_keystrokesopretdecode(g,b,p,k,spenttx.vout[1].scriptPubKey) == 'K' )
                            {
                                keystrokes = (char *)realloc(keystrokes,numkeys + (int32_t)k.size());
                                for (i=0; i<k.size(); i++)
                                    keystrokes[numkeys+i] = (char)k[i];
                                numkeys += (int32_t)k.size();
                                (*keystrokesp) = keystrokes;
                            }
                        }
                    }
                    //fprintf(stderr,"set baton %s\n",txid.GetHex().c_str());
                    batontxid = txid;
                    batonvout = 0; // not vini
                    // how to detect timeout, bailedout, highlander
                    hashBlock = zeroid;
                    if ( GetTransaction(batontxid,batontx,hashBlock,false) != 0 && batontx.vout.size() > 0 )
                    {
                        if ( hashBlock == zeroid )
                            batonht = komodo_nextheight();
                        else if ( (pindex= komodo_blockindex(hashBlock)) == 0 )
                            return(-4);
                        else batonht = pindex->GetHeight();
                        batonvalue = batontx.vout[0].nValue;
                        //printf("keystrokes[%d]\n",numkeys);
                        return(0);
                    }
                }
            } else fprintf(stderr,"findbaton opret error\n");
        }
        else
        {
            fprintf(stderr,"already played\n");
            return(-5);
        }
    }
    return(-1);
}

void rogue_gameplayerinfo(struct CCcontract_info *cp,UniValue &obj,uint256 gametxid,CTransaction gametx,int32_t vout,int32_t maxplayers,char *myrogueaddr)
{
    // identify if bailout or quit or timed out
    uint256 batontxid,spenttxid,gtxid,ptxid,hashBlock,playertxid; CTransaction spenttx,batontx; int32_t numplayers,regslot,numkeys,batonvout,batonht,retval; int64_t batonvalue; std::vector<uint8_t> playerdata; char destaddr[64];
    destaddr[0] = 0;
    if ( myIsutxo_spent(spenttxid,gametxid,vout) >= 0 )
    {
        if ( GetTransaction(spenttxid,spenttx,hashBlock,false) != 0 && spenttx.vout.size() > 0 )
            Getscriptaddress(destaddr,spenttx.vout[0].scriptPubKey);
    }
    obj.push_back(Pair("slot",(int64_t)vout-1));
    if ( (retval= rogue_findbaton(cp,playertxid,0,numkeys,regslot,playerdata,batontxid,batonvout,batonvalue,batonht,gametxid,gametx,maxplayers,destaddr,numplayers)) == 0 )
    {
        if ( CCgettxout(gametxid,maxplayers+vout,1) == 10000 )
        {
            if ( GetTransaction(batontxid,batontx,hashBlock,false) != 0 && batontx.vout.size() > 1 )
            {
                if ( rogue_registeropretdecode(gtxid,ptxid,batontx.vout[batontx.vout.size()-1].scriptPubKey) == 'R' && gtxid == gametxid && ptxid == playertxid )
                    obj.push_back(Pair("status","registered"));
                else obj.push_back(Pair("status","alive"));
            } else obj.push_back(Pair("status","error"));
        } else obj.push_back(Pair("status","finished"));
        obj.push_back(Pair("baton",batontxid.ToString()));
        obj.push_back(Pair("batonaddr",destaddr));
        obj.push_back(Pair("ismine",strcmp(myrogueaddr,destaddr)==0));
        obj.push_back(Pair("batonvout",(int64_t)batonvout));
        obj.push_back(Pair("batonvalue",ValueFromAmount(batonvalue)));
        obj.push_back(Pair("batonht",(int64_t)batonht));
        if ( playerdata.size() > 0 )
            obj.push_back(Pair("player",rogue_playerobj(playerdata,playertxid)));
    } else fprintf(stderr,"findbaton err.%d\n",retval);
}

int64_t rogue_registrationbaton(CMutableTransaction &mtx,uint256 gametxid,CTransaction gametx,int32_t maxplayers)
{
    int32_t vout,j,r; int64_t nValue;
    if ( gametx.vout.size() > maxplayers+1 )
    {
        r = rand() % maxplayers;
        for (j=0; j<maxplayers; j++)
        {
            vout = ((r + j) % maxplayers) + 1;
            if ( CCgettxout(gametxid,vout,1) == ROGUE_REGISTRATIONSIZE )
            {
                mtx.vin.push_back(CTxIn(gametxid,vout,CScript()));
                return(ROGUE_REGISTRATIONSIZE);
            }
        }
    }
    return(0);
}

UniValue rogue_rawtxresult(UniValue &result,std::string rawtx,int32_t broadcastflag)
{
    CTransaction tx;
    if ( rawtx.size() > 0 )
    {
        result.push_back(Pair("hex",rawtx));
        if ( DecodeHexTx(tx,rawtx) != 0 )
        {
            if ( broadcastflag != 0 && myAddtomempool(tx) != 0 )
                RelayTransaction(tx);
            result.push_back(Pair("txid",tx.GetHash().ToString()));
            result.push_back(Pair("result","success"));
        } else result.push_back(Pair("error","decode hex"));
    } else result.push_back(Pair("error","couldnt finalize CCtx"));
    return(result);
}

UniValue rogue_newgame(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ); std::string rawtx; CPubKey roguepk,mypk; char *jsonstr; uint64_t inputsum,change,required,buyin=0; int32_t i,n,maxplayers = 1;
    if ( txfee == 0 )
        txfee = 10000;
    if ( (params= cclib_reparse(&n,params)) != 0 )
    {
        if ( n > 0 )
        {
            maxplayers = juint(jitem(params,0),0);
            if ( n > 1 )
                buyin = jdouble(jitem(params,1),0) * COIN + 0.0000000049;
        }
    }
    if ( maxplayers < 1 || maxplayers > ROGUE_MAXPLAYERS )
        return(cclib_error(result,"illegal maxplayers"));
    mypk = pubkey2pk(Mypubkey());
    roguepk = GetUnspendable(cp,0);
    rogue_univalue(result,"newgame",maxplayers,buyin);
    required = (3*txfee + maxplayers*(ROGUE_REGISTRATIONSIZE+txfee));
    if ( (inputsum= AddCClibInputs(cp,mtx,roguepk,required,16,cp->unspendableCCaddr)) >= required )
    {
        mtx.vout.push_back(MakeCC1vout(cp->evalcode,txfee,roguepk)); // for highlander TCBOO creation
        for (i=0; i<maxplayers; i++)
            mtx.vout.push_back(MakeCC1of2vout(cp->evalcode,ROGUE_REGISTRATIONSIZE,roguepk,roguepk));
        for (i=0; i<maxplayers; i++)
            mtx.vout.push_back(MakeCC1of2vout(cp->evalcode,txfee,roguepk,roguepk));
        if ( (change= inputsum - required) >= txfee )
            mtx.vout.push_back(MakeCC1vout(cp->evalcode,change,roguepk));
        rawtx = FinalizeCCTx(0,cp,mtx,mypk,txfee,rogue_newgameopret(buyin,maxplayers));
        return(rogue_rawtxresult(result,rawtx,1));
    }
    else return(cclib_error(result,"illegal maxplayers"));
    return(result);
}

UniValue rogue_playerinfo(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    UniValue result(UniValue::VOBJ); std::vector<uint8_t> playerdata; uint256 playertxid,origplayergame;int32_t n; CPubKey pk; bits256 t;
    result.push_back(Pair("result","success"));
    rogue_univalue(result,"playerinfo",-1,-1);
    if ( (params= cclib_reparse(&n,params)) != 0 )
    {
        if ( n > 0 )
        {
            playertxid = juint256(jitem(params,0));
            if ( rogue_playerdata(cp,origplayergame,pk,playerdata,playertxid) < 0 )
                return(cclib_error(result,"invalid playerdata"));
            result.push_back(Pair("player",rogue_playerobj(playerdata,playertxid)));
        } else return(cclib_error(result,"no playertxid"));
        return(result);
    } else return(cclib_error(result,"couldnt reparse params"));
}

UniValue rogue_register(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    // vin0 -> ROGUE_REGISTRATIONSIZE 1of2 registration baton from creategame
    // vin1 -> optional nonfungible character vout @
    // vin2 -> original creation TCBOO playerdata used
    // vin3+ -> buyin
    // vout0 -> keystrokes/completion baton
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ); char destaddr[64],coinaddr[64]; uint256 gametxid,origplayergame,playertxid,hashBlock; int32_t err,maxplayers,gameheight,n,numvouts; int64_t inputsum,buyin,CCchange=0; CPubKey pk,mypk,roguepk; CTransaction tx,playertx; std::vector<uint8_t> playerdata; std::string rawtx; bits256 t;
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    roguepk = GetUnspendable(cp,0);
    rogue_univalue(result,"register",-1,-1);
    playertxid = zeroid;
    if ( (params= cclib_reparse(&n,params)) != 0 )
    {
        if ( n > 0 )
        {
            gametxid = juint256(jitem(params,0));
            if ( (err= rogue_isvalidgame(cp,gameheight,tx,buyin,maxplayers,gametxid)) == 0 )
            {
                if ( n > 1 )
                {
                    playertxid = juint256(jitem(params,1));
                    if ( rogue_playerdata(cp,origplayergame,pk,playerdata,playertxid) < 0 )
                        return(cclib_error(result,"couldnt extract valid playerdata"));
                }
                rogue_univalue(result,0,maxplayers,buyin);
                GetCCaddress1of2(cp,coinaddr,roguepk,mypk);
                if ( rogue_iamregistered(maxplayers,gametxid,tx,coinaddr) > 0 )
                    return(cclib_error(result,"already registered"));
                if ( (inputsum= rogue_registrationbaton(mtx,gametxid,tx,maxplayers)) != ROGUE_REGISTRATIONSIZE )
                    return(cclib_error(result,"couldnt find available registration baton"));
                else if ( playertxid != zeroid && rogue_playerdataspend(mtx,playertxid,origplayergame) < 0 )
                    return(cclib_error(result,"couldnt find playerdata to spend"));
                else if ( buyin > 0 && AddNormalinputs(mtx,mypk,buyin,64) < buyin )
                    return(cclib_error(result,"couldnt find enough normal funds for buyin"));
                if ( playertxid != zeroid )
                    AddNormalinputs2(mtx,txfee,10);
                mtx.vout.push_back(MakeCC1of2vout(cp->evalcode,buyin + inputsum - txfee,roguepk,mypk));
                GetCCaddress1of2(cp,destaddr,roguepk,roguepk);
                CCaddr1of2set(cp,roguepk,roguepk,cp->CCpriv,destaddr);
                mtx.vout.push_back(MakeTokensCC1vout(cp->evalcode, 1, CPubKey() /*nullpk*/));

                std::vector<uint8_t> vopretFinish, vopret2; uint8_t e, funcId; uint256 tokenid; std::vector<CPubKey> voutPubkeys, voutPubkeysEmpty; int32_t didtx = 0;
                CScript opretRegister = rogue_registeropret(gametxid, playertxid);
                if ( playertxid != zeroid )
                {
                    if ( GetTransaction(playertxid,playertx,hashBlock,false) != 0 )
                    {
                        if ( (funcId = DecodeTokenOpRet(playertx.vout.back().scriptPubKey, e, tokenid, voutPubkeys, vopretFinish, vopret2)) != 0)
                        {  // if token in the opret
                            didtx = 1;
                            if (funcId == 'c') // create tx itself
                                tokenid = playertxid;
                            rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee,
                                                 EncodeTokenOpRet(tokenid, voutPubkeysEmpty /*=never spent*/, vopretFinish /*=non-fungible*/, opretRegister));
                        }
                    }
                }
                if ( didtx == 0 )
                    rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee, opretRegister);

                return(rogue_rawtxresult(result,rawtx,1));
            } else return(cclib_error(result,"invalid gametxid"));
        } else return(cclib_error(result,"no gametxid"));
    } else return(cclib_error(result,"couldnt reparse params"));
}

UniValue rogue_keystrokes(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    // vin0 -> baton from registration or previous keystrokes
    // vout0 -> new baton
    // opret -> user input chars
    // being killed should auto broadcast (possible to be suppressed?)
    // respawn to be prevented by including timestamps
    int32_t nextheight = komodo_nextheight();
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(),nextheight);
    UniValue result(UniValue::VOBJ); CPubKey roguepk,mypk; uint256 gametxid,playertxid,batontxid; int64_t batonvalue,buyin; std::vector<uint8_t> keystrokes,playerdata; int32_t numplayers,regslot,numkeys,batonht,batonvout,n,elapsed,gameheight,maxplayers; CTransaction tx; CTxOut txout; char *keystrokestr,destaddr[64]; std::string rawtx; bits256 t; uint8_t mypriv[32];
    if ( txfee == 0 )
        txfee = 10000;
    rogue_univalue(result,"keystrokes",-1,-1);
    if ( (params= cclib_reparse(&n,params)) != 0 && n == 2 && (keystrokestr= jstr(jitem(params,1),0)) != 0 )
    {
        gametxid = juint256(jitem(params,0));
        keystrokes = ParseHex(keystrokestr);
        mypk = pubkey2pk(Mypubkey());
        roguepk = GetUnspendable(cp,0);
        GetCCaddress1of2(cp,destaddr,roguepk,mypk);
        if ( rogue_isvalidgame(cp,gameheight,tx,buyin,maxplayers,gametxid) == 0 )
        {
            if ( rogue_findbaton(cp,playertxid,0,numkeys,regslot,playerdata,batontxid,batonvout,batonvalue,batonht,gametxid,tx,maxplayers,destaddr,numplayers) == 0 )
            {
                if ( maxplayers == 1 || nextheight <= batonht+ROGUE_MAXKEYSTROKESGAP )
                {
                    mtx.vin.push_back(CTxIn(batontxid,batonvout,CScript()));
                    mtx.vout.push_back(MakeCC1of2vout(cp->evalcode,batonvalue-txfee,roguepk,mypk));
                    Myprivkey(mypriv);
                    CCaddr1of2set(cp,roguepk,mypk,mypriv,destaddr);
                    rawtx = FinalizeCCTx(0,cp,mtx,mypk,txfee,rogue_keystrokesopret(gametxid,batontxid,mypk,keystrokes));
                    //fprintf(stderr,"KEYSTROKES.(%s)\n",rawtx.c_str());
                    return(rogue_rawtxresult(result,rawtx,1));
                } else return(cclib_error(result,"keystrokes tx was too late"));
            } else return(cclib_error(result,"couldnt find batontxid"));
        } else return(cclib_error(result,"invalid gametxid"));
    } else return(cclib_error(result,"couldnt reparse params"));
}

UniValue rogue_finishgame(uint64_t txfee,struct CCcontract_info *cp,cJSON *params,char *method)
{
    //vin0 -> highlander vout from creategame TCBOO
    //vin1 -> keystrokes baton of completed game, must be last to quit or first to win, only spent registration batons matter. If more than 60 blocks since last keystrokes, it is forfeit
    //vins2+ -> rest of unspent registration utxo so all newgame vouts are spent
    //vout0 -> nonfungible character with pack @
    //vout1 -> 1% ingame gold and all the buyins

    // detect if last to bailout
    // vin0 -> kestrokes baton of completed game with Q
    // vout0 -> playerdata marker
    // vout0 -> 1% ingame gold
    // get any playerdata, get all keystrokes, replay game and compare final state
    CMutableTransaction mtx = CreateNewContextualCMutableTransaction(Params().GetConsensus(), komodo_nextheight());
    UniValue result(UniValue::VOBJ); std::string rawtx; CTransaction gametx; uint64_t seed,mult; int64_t buyin,batonvalue,inputsum,cashout,CCchange=0; int32_t i,err,gameheight,tmp,numplayers,regslot,n,num,numkeys,maxplayers,batonht,batonvout; char myrogueaddr[64],*keystrokes = 0; std::vector<uint8_t> playerdata,newdata; uint256 batontxid,playertxid,gametxid; CPubKey mypk,roguepk; uint8_t player[10000],mypriv[32],funcid;
    if ( txfee == 0 )
        txfee = 10000;
    mypk = pubkey2pk(Mypubkey());
    roguepk = GetUnspendable(cp,0);
    GetCCaddress1of2(cp,myrogueaddr,roguepk,mypk);
    result.push_back(Pair("name","rogue"));
    result.push_back(Pair("method",method));
    result.push_back(Pair("myrogueaddr",myrogueaddr));
    if ( strcmp(method,"bailout") == 0 )
    {
        funcid = 'Q';
        mult = 100000;
    }
    else
    {
        funcid = 'H';
        mult = 1000000;
    }
    if ( (params= cclib_reparse(&n,params)) != 0 )
    {
        if ( n > 0 )
        {
            gametxid = juint256(jitem(params,0));
            result.push_back(Pair("gametxid",gametxid.GetHex()));
            if ( (err= rogue_isvalidgame(cp,gameheight,gametx,buyin,maxplayers,gametxid)) == 0 )
            {
                if ( maxplayers == 1 )
                    mult /= 2;
                if ( rogue_findbaton(cp,playertxid,&keystrokes,numkeys,regslot,playerdata,batontxid,batonvout,batonvalue,batonht,gametxid,gametx,maxplayers,myrogueaddr,numplayers) == 0 )
                {
                    UniValue obj; struct rogue_player P;
                    seed = rogue_gamefields(obj,maxplayers,buyin,gametxid,myrogueaddr);
                    fprintf(stderr,"found baton %s numkeys.%d seed.%llu playerdata.%d\n",batontxid.ToString().c_str(),numkeys,(long long)seed,(int32_t)playerdata.size());
                    memset(&P,0,sizeof(P));
                    if ( playerdata.size() > 0 )
                    {
                        for (i=0; i<playerdata.size(); i++)
                            ((uint8_t *)&P)[i] = playerdata[i];
                    }
                    if ( keystrokes != 0 )
                    {
                        num = rogue_replay2(player,seed,keystrokes,numkeys,playerdata.size()==0?0:&P);
                        if ( keystrokes != 0 )
                            free(keystrokes);
                    } else num = 0;
                    mtx.vin.push_back(CTxIn(batontxid,batonvout,CScript()));
                    mtx.vin.push_back(CTxIn(gametxid,1+maxplayers+regslot,CScript()));
                    if ( funcid == 'H' )
                        mtx.vin.push_back(CTxIn(gametxid,0,CScript()));
                    if ( num > 0 )
                    {
                        newdata.resize(num);
                        for (i=0; i<num; i++)
                        {
                            newdata[i] = player[i];
                            ((uint8_t *)&P)[i] = player[i];
                        }
                        if ( P.gold <= 0 || P.hitpoints <= 0 || P.strength <= 0 || P.level <= 0 || P.experience <= 0 || P.dungeonlevel <= 0 )
                        {
                            fprintf(stderr,"zero value character was killed -> no playerdata\n");
                            newdata.resize(0);
                        }
                        else
                        {
                            mtx.vout.push_back(MakeTokensCC1vout(cp->evalcode,1,mypk));
                            fprintf(stderr,"\nextracted $$$gold.%d hp.%d strength.%d level.%d exp.%d dl.%d n.%d size.%d\n",P.gold,P.hitpoints,P.strength,P.level,P.experience,P.dungeonlevel,n,(int32_t)sizeof(P));
                            cashout = (uint64_t)P.gold * mult;
                            if ( funcid == 'H' && maxplayers > 1 )
                            {
                                if ( numplayers != maxplayers || (numplayers - rogue_playersalive(tmp,gametxid,maxplayers)) > 1 && (P.dungeonlevel > 1 || P.gold < 10000 || P.level < 20) )
                                    return(cclib_error(result,"highlander must be a winner or last one standing"));
                                cashout += numplayers * buyin;
                            }
                            if ( cashout >= txfee )
                            {
                                if ( (inputsum= AddCClibInputs(cp,mtx,roguepk,cashout,16,cp->unspendableCCaddr)) > (uint64_t)P.gold*mult )
                                    CCchange = (inputsum - cashout);
                                mtx.vout.push_back(CTxOut(cashout,CScript() << ParseHex(HexStr(mypk)) << OP_CHECKSIG));
                            }
                        }
                        //for (i=0; i<P.packsize; i++)
                        //    fprintf(stderr,"object (%s) type.%d pack.(%c:%d)\n",inv_name(o,FALSE),o->_o._o_type,o->_o._o_packch,o->_o._o_packch);
                    }
                    mtx.vout.push_back(MakeCC1vout(cp->evalcode,CCchange + (batonvalue-2*txfee),roguepk));
                    Myprivkey(mypriv);
                    CCaddr1of2set(cp,roguepk,mypk,mypriv,myrogueaddr);
                    CScript opret = rogue_highlanderopret(funcid, gametxid, regslot,mypk, newdata);
                    if ( newdata.size() == 0 )
                        rawtx = FinalizeCCTx(0,cp,mtx,mypk,txfee,opret);
                    else
                    {
                        char seedstr[32];
                        sprintf(seedstr,"%llu",(long long)seed);
                        std::vector<uint8_t> vopretNonfungible;
                        GetOpReturnData(opret, vopretNonfungible);
                        rawtx = FinalizeCCTx(0, cp, mtx, mypk, txfee, EncodeTokenCreateOpRet('c', Mypubkey(), std::string(seedstr), gametxid.GetHex(), vopretNonfungible));
                    }
                    return(rogue_rawtxresult(result,rawtx,0));
                }
                result.push_back(Pair("result","success"));
            } else fprintf(stderr,"illegal game err.%d\n",err);
        } else fprintf(stderr,"n.%d\n",n);
    }
    return(result);
}

UniValue rogue_bailout(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    return(rogue_finishgame(txfee,cp,params,"bailout"));
}

UniValue rogue_highlander(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    return(rogue_finishgame(txfee,cp,params,"highlander"));
}

UniValue rogue_gameinfo(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    UniValue result(UniValue::VOBJ),a(UniValue::VARR); int32_t i,n,gameheight,maxplayers,numvouts; uint256 txid; CTransaction tx; int64_t buyin; bits256 t; char myrogueaddr[64]; CPubKey mypk,roguepk;
    result.push_back(Pair("name","rogue"));
    result.push_back(Pair("method","gameinfo"));
    if ( (params= cclib_reparse(&n,params)) != 0 )
    {
        if ( n > 0 )
        {
            txid = juint256(jitem(params,0));
            result.push_back(Pair("gametxid",txid.GetHex()));
            if ( rogue_isvalidgame(cp,gameheight,tx,buyin,maxplayers,txid) == 0 )
            {
                result.push_back(Pair("result","success"));
                result.push_back(Pair("gameheight",(int64_t)gameheight));
                mypk = pubkey2pk(Mypubkey());
                roguepk = GetUnspendable(cp,0);
                GetCCaddress1of2(cp,myrogueaddr,roguepk,mypk);
                //fprintf(stderr,"myrogueaddr.%s\n",myrogueaddr);
                rogue_gamefields(result,maxplayers,buyin,txid,myrogueaddr);
                for (i=0; i<maxplayers; i++)
                {
                    if ( CCgettxout(txid,i+1,1) < 0 )
                    {
                        UniValue obj(UniValue::VOBJ);
                        rogue_gameplayerinfo(cp,obj,txid,tx,i+1,maxplayers,myrogueaddr);
                        a.push_back(obj);
                    }
                }
                result.push_back(Pair("players",a));
            } else return(cclib_error(result,"couldnt find valid game"));
        } else return(cclib_error(result,"couldnt parse params"));
    } else return(cclib_error(result,"missing txid in params"));
    return(result);
}

UniValue rogue_pending(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    UniValue result(UniValue::VOBJ),a(UniValue::VARR); int64_t buyin; uint256 txid,hashBlock; CTransaction tx; int32_t maxplayers,numplayers,gameheight,nextheight,vout,numvouts; CPubKey roguepk; char coinaddr[64];
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    roguepk = GetUnspendable(cp,0);
    GetCCaddress(cp,coinaddr,roguepk);
    SetCCunspents(unspentOutputs,coinaddr);
    nextheight = komodo_nextheight();
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        //char str[65]; fprintf(stderr,"%s check %s/v%d %.8f\n",coinaddr,uint256_str(str,txid),vout,(double)it->second.satoshis/COIN);
        if ( it->second.satoshis != txfee || vout != 0 ) // reject any that are not highlander markers
            continue;
        if ( rogue_isvalidgame(cp,gameheight,tx,buyin,maxplayers,txid) == 0 && gameheight > nextheight-777 )
        {
            rogue_playersalive(numplayers,txid,maxplayers);
            if ( numplayers < maxplayers )
                a.push_back(txid.GetHex());
        }
    }
    result.push_back(Pair("result","success"));
    rogue_univalue(result,"pending",-1,-1);
    result.push_back(Pair("pending",a));
    result.push_back(Pair("numpending",a.size()));
    return(result);
}

UniValue rogue_players(uint64_t txfee,struct CCcontract_info *cp,cJSON *params)
{
    UniValue result(UniValue::VOBJ),a(UniValue::VARR); int64_t buyin; uint256 gametxid,txid,hashBlock; CTransaction playertx,tx; int32_t maxplayers,vout,numvouts; std::vector<uint8_t> playerdata; CPubKey roguepk,mypk,pk; char coinaddr[64];
    std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> > unspentOutputs;
    roguepk = GetUnspendable(cp,0);
    mypk = pubkey2pk(Mypubkey());
    GetTokensCCaddress(cp,coinaddr,mypk);
    SetCCunspents(unspentOutputs,coinaddr);
    rogue_univalue(result,"players",-1,-1);
    for (std::vector<std::pair<CAddressUnspentKey, CAddressUnspentValue> >::const_iterator it=unspentOutputs.begin(); it!=unspentOutputs.end(); it++)
    {
        txid = it->first.txhash;
        vout = (int32_t)it->first.index;
        char str[65]; fprintf(stderr,"%s check %s/v%d %.8f\n",coinaddr,uint256_str(str,txid),vout,(double)it->second.satoshis/COIN);
        if ( it->second.satoshis != 1 || vout != 0 )
            continue;
        if ( rogue_playerdata(cp,gametxid,pk,playerdata,txid) == 0 && pk == mypk )
        {
            a.push_back(rogue_playerobj(playerdata,txid));
            //result.push_back(Pair("playerdata",rogue_playerobj(playerdata)));
        }
    }
    result.push_back(Pair("playerdata",a));
    result.push_back(Pair("numplayerdata",a.size()));
    return(result);
}
