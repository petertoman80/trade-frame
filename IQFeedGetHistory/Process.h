/************************************************************************
 * Copyright(c) 2010, One Unified. All rights reserved.                 *
 *                                                                      *
 * This file is provided as is WITHOUT ANY WARRANTY                     *
 *  without even the implied warranty of                                *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                *
 *                                                                      *
 * This software may not be used nor distributed without proper license *
 * agreement.                                                           *
 *                                                                      *
 * See the file LICENSE.txt for redistribution information.             *
 ************************************************************************/

#pragma once

/*
  handles the various messages for:
  * scanning a list of symbols from an exchange from the CInstrumentFile
  * obtaining history from IQFeed for each symbol
  * creating the appropriate structures 
  * processing the structures looking for promising trades
*/

#include <vector>
#include <string>

#include <LibTrading/InstrumentFile.h>

class CProcess
{
public:
  CProcess(void);
  ~CProcess(void);
  void Start( void );
protected:
private:
  CInstrumentFile m_IF;
  CInstrumentFile::iterator m_iterSymbols;

  std::vector<std::string> m_vExchanges;  // list of exchanges to be scanned
  std::vector<std::string> m_vSymbols;  // list of symbols to be scanned
};