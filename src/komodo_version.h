/******************************************************************************
 * Copyright © 2022 The SuperNET Developers.                                  *
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

#ifndef KOMODO_VERSION_H
#define KOMODO_VERSION_H

#include <string>
#include "clientversion.h"
#include "config/bitcoin-config.h"

// version = major * 1000000 + minor * 10000 + rev * 100 + build
//const int KOMODO_VERSION = 60000; 
//const int TOKEL_VERSION =  30100;

const std::string KOMODO_CLIENT_NAME = std::string("komodod:") + FormatVersion(KOMODO_VERSION);
const std::string TOKEL_CLIENT_NAME = std::string("tokeld:") + FormatVersion(TOKEL_VERSION);


#endif // #ifndef KOMODO_VERSION_H
