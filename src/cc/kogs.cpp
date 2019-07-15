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

#include "CCKogs.h"


UniValue KogsCreatePlayer()
{

}


UniValue KogsCreateKogs(std::vector<Kog> & newkogs)
{
    srand(time(NULL));

    for (auto &k : newkogs) {

        int32_t borderColor = rand() % 26 + 1;
        int32_t borderGraphic = rand() % 12;
        int32_t borderWidth = rand() % 15 + 1;

        // generate the border appearance id:
        k.appearanceId = borderColor * 12 * 16 + borderGraphic * 16 + borderWidth;


        //k.appearanceId 
    }

}
