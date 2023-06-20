/******************************************************************************
 * Copyright © 2014-2023 The SuperNET Developers.                             *
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

#include "komodo_structs.h"
#include "komodo_defs.h"
#include "testutils.h"
#include "chainparams.h"
#include "chain.h"

#include <gtest/gtest.h>

int32_t komodo_electednotary(int32_t *numnotariesp,uint8_t *pubkey33,int32_t height,uint32_t timestamp);


static void komodo_notaries_uninit()
{
}


namespace TestNotary
{

/***
 * A little class to help with the different formats keys come in
 */
class my_key
{
public:
    my_key(uint8_t in[33])
    {
        key.clear();
        for(int i = 0; i < 33; ++i)
            key.push_back(in[i]);
    }
    my_key(const std::string& in)
    {
        key.clear();
        for(int i = 0; i < 33; ++i)
            key.push_back( 
                    (unsigned int)strtol(in.substr(i*2, 2).c_str(), nullptr, 16) );
    }
    bool fill(uint8_t in[33])
    {
        memcpy(in, key.data(), 33);
        return true;
    }
private:
    std::vector<uint8_t> key;
    friend bool operator==(const my_key& lhs, const my_key& rhs);
};

bool operator==(const my_key& lhs, const my_key& rhs)
{
    if (lhs.key == rhs.key)
        return true;
    return false;
}

TEST(TestNotary, KomodoNotaries_3P)
{
    // Test komodo_notaries(), getkmdseason()
    strncpy(ASSETCHAINS_SYMBOL, "TOKEL", sizeof(ASSETCHAINS_SYMBOL)); // set as marmara chain
    ASSETCHAINS_CC = 2;

    SelectParams(CBaseChainParams::MAIN);
    komodo_notaries_uninit();
    uint8_t pubkeys[64][33];
    int32_t result;

    uint32_t timestamp_S1 = 1525132800;
    result = komodo_notaries(pubkeys, 0, timestamp_S1);
    EXPECT_EQ(result, 64);
    EXPECT_EQ( getacseason(timestamp_S1), 1);
    EXPECT_EQ( my_key(pubkeys[3]), my_key("029acf1dcd9f5ff9c455f8bb717d4ae0c703e089d16cf8424619c491dff5994c90"));

    uint32_t timestamp_S2 = timestamp_S1 + 1;
    result = komodo_notaries(pubkeys, 0, timestamp_S2);
    EXPECT_EQ(result, 64);
    EXPECT_EQ( getacseason(timestamp_S2), 2);
    EXPECT_EQ( my_key(pubkeys[1]), my_key("030f34af4b908fb8eb2099accb56b8d157d49f6cfb691baa80fdd34f385efed961"));

}

TEST(TestNotary, ElectedNotary3P)
{
    // exercise the routine that checks to see if a particular public key is a notary at the current height

    SelectParams(CBaseChainParams::MAIN);
    // setup
    komodo_notaries_uninit();
    my_key nn_pk1("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344"); // S1
    my_key nn_pk2("030f34af4b908fb8eb2099accb56b8d157d49f6cfb691baa80fdd34f385efed961"); // S2
    my_key pk_wrong("030f34af4b908fb8eb2099accb56b8d157d49f6cfb691baa80fdd34f385efeabab");

    int32_t numnotaries;
    uint8_t pubkey[33];
    nn_pk1.fill(pubkey);
    uint32_t timestamp_S1 = 1525132800;
    uint32_t timestamp_S2 = 1563148800;

    // check the KMD chain, first era
    strncpy(ASSETCHAINS_SYMBOL, "TOKEL", sizeof(ASSETCHAINS_SYMBOL)); // set as marmara chain
    ASSETCHAINS_CC = 2;

    int32_t result = komodo_electednotary(&numnotaries, pubkey, 0, timestamp_S1);
    EXPECT_EQ(result, 1);
    EXPECT_EQ( numnotaries, 64);
    // now try a wrong key
    pk_wrong.fill(pubkey);
    result = komodo_electednotary(&numnotaries, pubkey, 0, timestamp_S1);
    EXPECT_EQ(result, -1);
    EXPECT_EQ(numnotaries, 64);

    nn_pk2.fill(pubkey); 
    result = komodo_electednotary(&numnotaries, pubkey, 0, timestamp_S2);
    EXPECT_EQ(result, 1);
    EXPECT_EQ( numnotaries, 64);

    // now try a wrong key
    pk_wrong.fill(pubkey);
    result = komodo_electednotary(&numnotaries, pubkey, 0, timestamp_S2);
    EXPECT_EQ(result, -1);
    EXPECT_EQ(numnotaries, 64);
}


// season 7 tests for asset chain:
TEST(TestNotary, KomodoNotaries_S7_3P)
{
    uint8_t pubkeys[64][33];

    SelectParams(CBaseChainParams::MAIN);
    komodo_notaries_uninit();
    strncpy(ASSETCHAINS_SYMBOL, "TOKEL", sizeof(ASSETCHAINS_SYMBOL)); // set as marmara chain
    ASSETCHAINS_CC = 2;

    uint32_t timestamp_S7 = 1688132253+1; // first S7 sec
    uint32_t timestamp_EOL = 1851328000; // last S7 sec

    EXPECT_EQ( getacseason(timestamp_S7), 8); // S7 has index 8
    EXPECT_EQ( getacseason(timestamp_EOL), 8);
    EXPECT_EQ( getacseason(timestamp_EOL+1), 0);

    int32_t result1 = komodo_notaries(pubkeys, 0, timestamp_S7);
    EXPECT_EQ(result1, 64);
    EXPECT_EQ( my_key(pubkeys[0]), my_key("035509136135ba8e3f5d4733f7a9c160c2e1fefd8dc4658c3d95a5407e8da14749")); // s7 pk 0
    EXPECT_EQ( my_key(pubkeys[63]), my_key("02a473e980bf0d198ece8ed11f1ecbe437edb688de6c83b82efa6f7de3a5d43c19")); // s7 pk 63

    {
        int32_t numnotaries;
        my_key pk2("032674b15524dab1c7a5824aa9d3d38f231a8a04095e11920677ee99d8197d9c60"); // s7 pk 2
        uint8_t pubkey[33];
        pk2.fill(pubkey);
        int32_t result2 = komodo_electednotary(&numnotaries, pubkey, 0, timestamp_S7);
        EXPECT_EQ(result2, 1);
        EXPECT_EQ(numnotaries, 64);
    }

    // try wrong pubkey
    {
        int32_t numnotaries;
        my_key wrong_pk("02ebfc784a4ba768aad88d44d1045d240d47b26e248cafaf1c5169a42d7a61d344");
        uint8_t pubkey[33];
        wrong_pk.fill(pubkey);
        int32_t result2 = komodo_electednotary(&numnotaries, pubkey, 0, timestamp_S7);
        EXPECT_EQ(result2, -1);
        EXPECT_EQ(numnotaries, 64);
    }

    // cleanup
    komodo_notaries_uninit();
}

} // namespace TestNotary