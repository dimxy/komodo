/******************************************************************************
* Copyright � 2014-2019 The SuperNET Developers.                             *
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

#ifndef __PRICES_FEED__
#define __PRICES_FEED__

#include <string>
#include <vector>
#include <stdint.h>

#include <cJSON.h>

// support for creating data feeds for prices cc

#define PF_BUFOVERFLOW 0xFFFFFFFF

struct CFeedConfigItem {

    std::string name;
    std::string url;
    std::vector<std::string> substitutes;
    std::string base;

    struct 
    {
        std::string symbolpath;
        std::string valuepath;
    } resultDesc;

    struct 
    {
        std::vector<std::string> symbols;
        bool isPath;
        std::vector<std::string> valuepaths;
    } resultsDesc;

    int32_t interval;
    int32_t multiplier;
};

bool PricesFeedParseConfig(const cJSON *json);
uint32_t ParseFeedTotalSize(void);
uint32_t PricesFeedPoll(uint32_t *pricevalues, uint32_t maxsize, time_t *timestamp);

#endif // #ifndef __PRICES_FEED__