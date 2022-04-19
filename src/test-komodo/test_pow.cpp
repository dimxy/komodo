
#include "consensus/validation.h"
#include "main.h"

#include "chain.h"
#include "chainparams.h"
#include "pow.h"
#include "random.h"
#include <gtest/gtest.h>

extern int8_t ASSETCHAINS_ADAPTIVEPOW;

TEST(PoW, DifficultyAveraging) {
    SelectParams(CBaseChainParams::MAIN);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].SetHeight(i);
        blocks[i].nTime = 1269211443 + i * params.nPowTargetSpacing;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].chainPower = i ? (CChainPower(&blocks[i]) + blocks[i - 1].chainPower) + GetBlockProof(blocks[i - 1]) : CChainPower(&blocks[i]);
    }

    // Result should be the same as if last difficulty was used
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= params.AveragingWindowTimespan();
    EXPECT_EQ(bnRes.GetCompact(), GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Randomise the final block time (plus 1 to ensure it is always different)
    blocks[lastBlk].nTime += GetRand(params.nPowTargetSpacing/2) + 1;

    // Result should be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should not be unchanged
    EXPECT_NE(0x1e7fffff, GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Change the final block difficulty
    blocks[lastBlk].nBits = 0x1e0fffff;

    // Result should not be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_NE(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Result should be the same as if the average difficulty was used
    arith_uint256 average = UintToArith256(uint256S("0000796968696969696969696969696969696969696969696969696969696969"));
    EXPECT_EQ(CalculateNextWorkRequired(average,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
}

TEST(PoW, MinDifficultyRules) {
    SelectParams(CBaseChainParams::TESTNET);
    const Consensus::Params& params = Params().GetConsensus();
    size_t lastBlk = 2*params.nPowAveragingWindow;
    const uint32_t startTime = 1269211443;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    uint32_t nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + params.nPowTargetSpacing;
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].SetHeight(params.nPowAllowMinDifficultyBlocksAfterHeight.get() + i);
        blocks[i].nTime = nextTime;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }

    // Create a new block at the target spacing
    CBlockHeader next;
    next.nTime = blocks[lastBlk].nTime + params.nPowTargetSpacing;

    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= params.AveragingWindowTimespan();
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block a bit, time warp protection should prevent any change
    next.nTime += params.nPowTargetSpacing * 5;

    // Result should be unchanged, modulo integer division precision loss
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // Delay last block to a huge number. Result should be unchanged, time warp protection
    next.nTime = std::numeric_limits<uint32_t>::max();
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), bnRes.GetCompact());

    // space all blocks out so the median is above the limits and difficulty should drop
    nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + ( params.MaxActualTimespan() / params.nPowAveragingWindow + 1);
        blocks[i].nTime = nextTime;
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }

    // difficulty should have decreased ( nBits increased )
    EXPECT_GT(GetNextWorkRequired(&blocks[lastBlk], &next, params),
            bnRes.GetCompact());

    // diffuculty should never decrease below minimum
    arith_uint256 minWork = UintToArith256(params.powLimit);
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].nBits = minWork.GetCompact();
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }
    EXPECT_EQ(GetNextWorkRequired(&blocks[lastBlk], &next, params), minWork.GetCompact());

    // space all blocks out so the median is under limits and difficulty should increase
    nextTime = startTime;
    for (int i = 0; i <= lastBlk; i++) {
        nextTime = nextTime + (params.MinActualTimespan() / params.nPowAveragingWindow - 1);
        blocks[i].nTime = nextTime;
        blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        blocks[i].chainPower.chainWork = i ? blocks[i - 1].chainPower.chainWork 
                + GetBlockProof(blocks[i - 1]).chainWork : arith_uint256(0);
    }

    // difficulty should have increased ( nBits decreased )
    EXPECT_LT(GetNextWorkRequired(&blocks[lastBlk], &next, params),
            bnRes.GetCompact());

}




CMutableTransaction GetFirstBlockCoinbaseTx(int32_t h) {
    CMutableTransaction mtx;

    // No inputs.
    mtx.vin.resize(1);
    mtx.vin[0].prevout.SetNull();

    // Set height to h.
    mtx.vin[0].scriptSig = CScript() << h << OP_0;

    // Give it a single zero-valued, always-valid output.
    mtx.vout.resize(1);
    mtx.vout[0].scriptPubKey = CScript() << OP_TRUE;
    mtx.vout[0].nValue = 0;

    /*if (h > 0) {
        // Give it a Founder's Reward vout for height h.
        auto rewardScript = Params().GetFoundersRewardScriptAtHeight(h);
        mtx.vout.push_back(CTxOut(
                    GetBlockSubsidy(h, Params().GetConsensus())/5,
                    rewardScript));
    }*/

    return mtx;
}

extern double GetDifficulty(const CBlockIndex* blockindex = NULL);

void TestDifficultyAveragingImplDimxy(const Consensus::Params& params)
{
    size_t lastBlk = (2+200)*params.nPowAveragingWindow;
    size_t firstBlk = lastBlk - params.nPowAveragingWindow;

    // Start with blocks evenly-spaced and equal difficulty
    std::vector<CBlockIndex> blocks(lastBlk+1);
    //arith_uint256 hashrate1 { 70LL*5* 1024*1024*1024*1024 };
    //arith_uint256 hashrate2 { 100LL* 70*5* 1024*1024*1024*1024 };
    arith_uint256 hashrate1 { 50LL * 100*1024 };
    arith_uint256 hashrate2 { (5000LL +  50LL) * 100*1024 };


    arith_uint256 max256 = UintToArith256(uint256S("ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"));
    arith_uint256 powlimit256 = UintToArith256(Params().GetConsensus().powLimit);

    std::cerr << "Starting difficulty testing, ASSETCHAINS_ADAPTIVEPOW is " << (int)ASSETCHAINS_ADAPTIVEPOW << std::endl;
    for (int i = 0; i <= lastBlk; i++) {
        blocks[i].pprev = i ? &blocks[i - 1] : nullptr;
        blocks[i].SetHeight(i+1);

        /*
        if (i == 0)
            blocks[i].nTime = Params().GenesisBlock().nTime + params.nPowTargetSpacing; // 1269211443;
        else if (i < 75)
            blocks[i].nTime = blocks[i - 1].nTime + params.nPowTargetSpacing;
        else if (i < 115) {
            //std::cerr << __func__ << " i=" << i << " params.PoWTargetSpacing(i)=" << params.PoWTargetSpacing(i) << " params.PoWTargetSpacing(i) * 0.01=" << params.PoWTargetSpacing(i) * 0.01 << std::endl;
            blocks[i].nTime = blocks[i - 1].nTime + (params.nPowTargetSpacing * 0.2 );
            //std::cerr << __func__ << " i=" << i << " blocks[i].nTime=" << blocks[i].nTime << std::endl;
        } else {
            blocks[i].nTime = blocks[i - 1].nTime + params.nPowTargetSpacing * 2;
        }*/
        double difficulty = 1;
        if (i > 0)
            difficulty = GetDifficulty(&blocks[i - 1]);
        assert(difficulty < (double)0x7fffffffffffffffLL);
        arith_uint256 diff256 { (uint64_t)(difficulty) };
        arith_uint256 target = powlimit256 / diff256;


        arith_uint256 work = max256 / (target + 1);

        //arith_uint256 mult2pow32 { 0x100000000 / 0x100000000 }; // for btc
        int64_t timeToGetBlock = 0;
        if (i == 0)
            blocks[i].nTime = Params().GenesisBlock().nTime + params.nPowTargetSpacing; 
        else if (i > 1500 && i < 3000) {
            //arith_uint256 timeToGetBlock256 { diff256 * mult2pow32 / hashrate2 };  // btc
            arith_uint256 timeToGetBlock256 = work / hashrate2;
            assert (timeToGetBlock256.getdouble() < (double)0x7fffffffffffffffLL);
            timeToGetBlock = timeToGetBlock256.GetLow64();
            blocks[i].nTime = blocks[i - 1].nTime + timeToGetBlock;
        }
        else {
            // arith_uint256 timeToGetBlock256 { diff256 * mult2pow32 / hashrate1 }; // btc
            arith_uint256 timeToGetBlock256 = work / hashrate1;
            assert (timeToGetBlock256.getdouble() < (double)0x7fffffffffffffffLL);
            timeToGetBlock = timeToGetBlock256.GetLow64();
            blocks[i].nTime = blocks[i - 1].nTime + timeToGetBlock;
        } 
        std::cerr << "i," << i << ",difficulty," << std::fixed << std::setprecision(6) << difficulty << ",timeToGetBlock," << timeToGetBlock << std::endl;

        //MockCValidationState state;
        CBlockIndex indexGen {Params().GenesisBlock()};
        CValidationState state;
        CBlockIndex *pindexPrev = i ? blocks[i].pprev : &indexGen;
        CBlockHeader blockPrev;
        if (i)  {
            blockPrev.nTime = blocks[i-1].nTime;
            blockPrev.nBits = blocks[i-1].nBits;
        }
        else {
            blockPrev.nTime = Params().GenesisBlock().nTime;
            blockPrev.nBits = Params().GenesisBlock().nBits;
        }

        //blocks[i].nBits = 0x1e7fffff; /* target 0x007fffff000... */
        if (i)  
            blocks[i].nBits = GetNextWorkRequired(&blocks[i-1], &blockPrev, params);
        else 
            blocks[i].nBits = Params().GenesisBlock().nBits;
        blocks[i].chainPower = i ? (CChainPower(&blocks[i]) + blocks[i - 1].chainPower) + GetBlockProof(blocks[i - 1]) : CChainPower(&blocks[i]);


        arith_uint256  bnThis;
        bnThis.SetCompact( blocks[i].nBits );
        //std::cerr << __func__ << " i=" << i << "      target=" << ArithToUint256(target).GetHex() << std::endl;
        //std::cerr << __func__ << " i=" << i << " bits target=" << ArithToUint256(bnThis).GetHex() << std::endl;
        //std::cerr  << "i," << std::dec << i << "," << (bnThis.GetCompact() & 0x00ffffff) << "," << (bnThis.GetCompact() >> 24)  << "," << ArithToUint256(bnThis).GetHex() << std::endl;


        CMutableTransaction mtx = GetFirstBlockCoinbaseTx(blocks[i].GetHeight());
        //mtx.vin[0].scriptSig = CScript() << OP_0;
        //mtx.vout.pop_back(); // remove the FR output

        CBlock block;
        block.vtx.push_back(mtx);
        block.nTime = blocks[i].nTime;
        block.nBits = blocks[i].nBits;


        //EXPECT_CALL(state, DoS(level, false, REJECT_INVALID, reason, false, "")).Times(1);

        /*bool fCheckBlockHeader = ContextualCheckBlockHeader(block, state, pindexPrev);
        if (!fCheckBlockHeader) std::cerr << __func__ << " i=" << i << " ContextualCheckBlockHeader state=" << state.GetRejectReason() << std::endl;
        EXPECT_TRUE(fCheckBlockHeader);

        bool fCheckBlock = ContextualCheckBlock(true, block, state, pindexPrev);
        if (!fCheckBlock) std::cerr << __func__ << " i=" << i << " ContextualCheckBlock state=" << state.GetRejectReason() << std::endl;
        EXPECT_TRUE(fCheckBlock);*/
    }

    // Result should be the same as if last difficulty was used
    arith_uint256 bnAvg;
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    /*EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));*/

/*
    std::cerr << __func__ << " CalculateNextWorkRequired=" << std::hex << CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params) << std::endl;
    arith_uint256 bnNext;
    bnNext.SetCompact( GetNextWorkRequired(&blocks[lastBlk], nullptr, params) );
    //std::cerr << __func__ << " GetNextWorkRequired=" << std::hex << bnNext.GetCompact() << " " << ArithToUint256(bnNext).GetHex() << std::endl;
    // Result should be unchanged, modulo integer division precision loss
    arith_uint256 bnRes;
    bnRes.SetCompact(0x1e7fffff);
    bnRes /= params.AveragingWindowTimespan();
    bnRes *= params.AveragingWindowTimespan();

    //std::cerr << __func__ << " GetCompact=" << std::hex << bnRes.GetCompact() << " " << ArithToUint256(bnRes).GetHex() << std::endl;
*/
    EXPECT_TRUE(true);
/*    EXPECT_EQ(bnRes.GetCompact(), GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Randomise the final block time (plus 1 to ensure it is always different)
    blocks[lastBlk].nTime += GetRand(params.PoWTargetSpacing(blocks[lastBlk].nHeight + 1)/2) + 1;

    // Result should be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_EQ(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));
    // Result should not be unchanged
    EXPECT_NE(0x1e7fffff, GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Change the final block difficulty
    blocks[lastBlk].nBits = 0x1e0fffff;

    // Result should not be the same as if last difficulty was used
    bnAvg.SetCompact(blocks[lastBlk].nBits);
    EXPECT_NE(CalculateNextWorkRequired(bnAvg,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));

    // Result should be the same as if the average difficulty was used
    arith_uint256 average = UintToArith256(uint256S("0000796968696969696969696969696969696969696969696969696969696969"));
    EXPECT_EQ(CalculateNextWorkRequired(average,
                                        blocks[lastBlk].GetMedianTimePast(),
                                        blocks[firstBlk].GetMedianTimePast(),
                                        params,
                                        blocks[lastBlk].nHeight + 1),
              GetNextWorkRequired(&blocks[lastBlk], nullptr, params));*/
}


TEST(PoW, DifficultyAveragingDimxy) {
    SelectParams(CBaseChainParams::MAIN);
    //ASSETCHAINS_ADAPTIVEPOW = 6;
    TestDifficultyAveragingImplDimxy(Params().GetConsensus());
}
