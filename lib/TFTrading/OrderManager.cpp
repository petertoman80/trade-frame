/************************************************************************
 * Copyright(c) 2009, One Unified. All rights reserved.                 *
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

#include "StdAfx.h"

#include "OrderManager.h"

namespace ou { // One Unified
namespace tf { // TradeFrame

//
// COrderManager
//

COrderManager::COrderManager(void) 
: m_orderIds( Trading::DbFileName, "OrderId" )  // need to remove dependency on DB4 and migrate to sql
{
}

COrderManager::~COrderManager(void) {
}

COrder::idOrder_t COrderManager::CheckOrderId( idOrder_t id ) {
  idOrder_t oldId = m_orderIds.GetCurrentId();
  if ( id > oldId ) {
    m_orderIds.SetNextId( id );
  }
  return oldId;
}

COrderManager::pOrder_t COrderManager::ConstructOrder( // market order
    CInstrument::pInstrument_cref instrument, 
    OrderType::enumOrderType eOrderType, OrderSide::enumOrderSide eOrderSide, 
    boost::uint32_t nOrderQuantity, 
    idPosition_t idPosition
    ) {
  assert( nOrderQuantity > 0 );
  pOrder_t pOrder( new COrder( instrument,  eOrderType, eOrderSide, nOrderQuantity, idPosition ) );
  ConstructOrder( pOrder );
  return pOrder;
}

COrderManager::pOrder_t COrderManager::ConstructOrder( // limit or stop
    CInstrument::pInstrument_cref instrument, 
    OrderType::enumOrderType eOrderType, OrderSide::enumOrderSide eOrderSide, 
    boost::uint32_t nOrderQuantity, double dblPrice1,  
    idPosition_t idPosition
    ) {
  assert( nOrderQuantity > 0 );
  assert( dblPrice1 > 0 );
  pOrder_t pOrder( new COrder( instrument, eOrderType, eOrderSide, nOrderQuantity, dblPrice1, idPosition ) );
  ConstructOrder( pOrder );
  return pOrder;
}

COrderManager::pOrder_t COrderManager::ConstructOrder( // limit and stop
    CInstrument::pInstrument_cref instrument, 
    OrderType::enumOrderType eOrderType, OrderSide::enumOrderSide eOrderSide, 
    boost::uint32_t nOrderQuantity, double dblPrice1, double dblPrice2,
    idPosition_t idPosition
    ) {
  assert( nOrderQuantity > 0 );
  assert( dblPrice1 > 0 );
  assert( dblPrice2 > 0 );
  pOrder_t pOrder( new COrder( instrument, eOrderType, eOrderSide, nOrderQuantity, dblPrice1, dblPrice2, idPosition ) );
  ConstructOrder( pOrder );
  return pOrder;
}

void COrderManager::ConstructOrder( pOrder_t& pOrder ) {
  // obtain an order id, then insert into maps, may need to change how this happens later in order to get rid of db4
  idOrder_t id = m_orderIds.GetNextId();
  pOrder->SetOrderId( id );
  // need to create an exception free way to check that order does not exist, in the meantime:
  try {
    iterOrders_t iter = LocateOrder( id );
    if ( m_mapOrders.end() != iter ) {
      throw std::runtime_error( "COrderManager::ConstructOrder: order id already exists" );
    }
  }
  catch (...) {
  }
  
  pairOrderState_t pair( id, structOrderState( pOrder ) );
  m_mapOrders.insert( pair );

  if ( 0 != m_pDbSession ) {
    // add to database
    assert( 0 != pOrder->GetRow().idPosition );
    ou::db::QueryFields<COrder::TableRowDef>::pQueryFields_t pQuery
      = m_pDbSession->Insert<COrder::TableRowDef>( const_cast<COrder::TableRowDef&>( pOrder->GetRow() ) );
  }
}

namespace OrderManagerQueries {
  struct UpdateAtPlaceOrder {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "orderstatus", eOrderStatus );
      ou::db::Field( a, "datetimesubmitted", dtOrderSubmitted );
      ou::db::Field( a, "orderid", idOrder );
    }
    COrder::idOrder_t idOrder;
    ptime dtOrderSubmitted;
    OrderStatus::enumOrderStatus eOrderStatus;
    UpdateAtPlaceOrder( COrder::idOrder_t id, OrderStatus::enumOrderStatus status, ptime dtOrderSubmitted_ )
      : idOrder( id ), dtOrderSubmitted( dtOrderSubmitted_ ), eOrderStatus( status ) {};
  };
}

void COrderManager::PlaceOrder(CProviderInterfaceBase *pProvider, pOrder_t pOrder) {

  iterOrders_t iter = LocateOrder( pOrder->GetOrderId() );
  if ( m_mapOrders.end() == iter ) {
    throw std::runtime_error( "COrderManager::PlaceOrder:  order is not current in the order manager" );
  }

  assert( NULL != pProvider );
  iter->second.pProvider = pProvider;
  pOrder->SetSendingToProvider();
  pProvider->PlaceOrder( pOrder );
  if ( 0 != m_pDbSession ) {
    OrderManagerQueries::UpdateAtPlaceOrder 
      update( pOrder->GetOrderId(), pOrder->GetRow().eOrderStatus, pOrder->GetRow().dtOrderSubmitted );
    ou::db::QueryFields<OrderManagerQueries::UpdateAtPlaceOrder>::pQueryFields_t pQuery
      = m_pDbSession->SQL<OrderManagerQueries::UpdateAtPlaceOrder>( // todo:  cache this query
        "update orders set orderstatus=?, datetimesubmitted=?", update ).Where( "orderid=?" );
  }
}

namespace OrderManagerQueries {
  struct OrderKey {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "orderid", idOrder );
    }
    ou::tf::keytypes::idOrder_t idOrder;
    OrderKey( ou::tf::keytypes::idOrder_t idOrder_ ): idOrder( idOrder_ ) {};
  };

  struct ExecutionKey {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "executionid", idExecution );
    }
    ou::tf::keytypes::idExecution_t idExecution;
    ExecutionKey( ou::tf::keytypes::idExecution_t idExecution_ ): idExecution( idExecution_ ) {};
  };
}

COrderManager::iterOrders_t COrderManager::LocateOrder( idOrder_t nOrderId ) {
  // if not in memory, the load order and executions from disk
  bool bFound = false;
  iterOrders_t iter = m_mapOrders.find( nOrderId );
  if ( m_mapOrders.end() != iter ) {
    bFound = true;
  }
  else {
    // check in database first, and if found, load order and executions
    if ( 0 != m_pDbSession ) {
      OrderManagerQueries::OrderKey keyOrder( nOrderId );
      ou::db::QueryFields<OrderManagerQueries::OrderKey>::pQueryFields_t pOrderExistsQuery
        = m_pDbSession->SQL<OrderManagerQueries::OrderKey>( "select * from orders", keyOrder ).Where( "orderid=?" ).NoExecute();
      m_pDbSession->Bind<OrderManagerQueries::OrderKey>( pOrderExistsQuery );
      if ( m_pDbSession->Execute( pOrderExistsQuery ) ) {
        bool bFound = true;
        // load order as well as associated executions
        COrder::TableRowDef rowOrder;
        m_pDbSession->Columns<OrderManagerQueries::OrderKey, COrder::TableRowDef>( pOrderExistsQuery, rowOrder );
        if ( 0 == OnOrderNeedsDetails ) {
          throw std::runtime_error( "COrderManager::LocateOrder: needs Order Details Callback" );
        }
        pInstrument_t pInstrument;
        OnOrderNeedsDetails( rowOrder.idInstrument, pInstrument );
        pOrder_t pOrder( new COrder( rowOrder, pInstrument ) );
        std::pair<iterOrders_t, bool> response;
        response = m_mapOrders.insert( pairOrderState_t(rowOrder.idOrder, structOrderState( pOrder ) ) );
        if ( false == response.second ) {
          throw std::runtime_error( "COrderManager::LocateOrder:  couldn't insert order into map" );
        }
        iter = response.first;

        // load up executions
        ou::db::QueryFields<OrderManagerQueries::OrderKey>::pQueryFields_t pExecutionQuery
          = m_pDbSession->SQL<OrderManagerQueries::OrderKey>( "select * from executions", keyOrder ).Where( "orderid=?" ).NoExecute();
        while ( m_pDbSession->Execute( pExecutionQuery ) ) {
          CExecution::TableRowDef rowExecution;
          m_pDbSession->Columns<OrderManagerQueries::OrderKey, CExecution::TableRowDef>( pExecutionQuery, rowExecution );
          pExecution_t pExecution( new CExecution( rowExecution ) );
          iter->second.pmapExecutions->insert( pairExecution_t( rowExecution.idExecution, pExecution ) );
        }
      }
    }
  }
  if ( !bFound ) {
    std::cout << "COrderManager::LocateOrder order not found:  " << nOrderId << std::endl;
    throw std::out_of_range( "OrderId not found" );
  }
  return iter;
}

namespace OrderManagerQueries {
  struct UpdateAtOrderClose {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "orderstatus", eOrderStatus );
      ou::db::Field( a, "datetimeclosed", dtOrderClosed );
      ou::db::Field( a, "orderid", idOrder );
    }
    COrder::idOrder_t idOrder;
    ptime dtOrderClosed;
    OrderStatus::enumOrderStatus eOrderStatus;
    UpdateAtOrderClose( COrder::idOrder_t id, OrderStatus::enumOrderStatus status, ptime dtOrderClosed_ )
      : idOrder( id ), dtOrderClosed( dtOrderClosed_ ), eOrderStatus( status ) {};
  };
}

void COrderManager::CancelOrder( idOrder_t nOrderId) {
  try {
    mapOrders_t::iterator iter = LocateOrder( nOrderId );
    pOrder_t pOrder = iter->second.pOrder;
    iter->second.pProvider->CancelOrder( pOrder );  // check which fields have changed for the db
    if ( 0 != m_pDbSession ) {
      OrderManagerQueries::UpdateAtOrderClose 
        close( pOrder->GetOrderId(), pOrder->GetRow().eOrderStatus, pOrder->GetRow().dtOrderClosed );
      ou::db::QueryFields<OrderManagerQueries::UpdateAtOrderClose>::pQueryFields_t pQuery
        = m_pDbSession->SQL<OrderManagerQueries::UpdateAtOrderClose>( // todo:  cache this query
          "update orders set orderstatus=?, datetimeclosed=?", close ).Where( "orderid=?" );
    }
  }
  catch (...) {
    std::cout << "Problems in COrderManager::CancelOrder" << std::endl;
  }
}

namespace OrderManagerQueries {
  struct UpdateCommission {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "commission", dblCommission );
      ou::db::Field( a, "orderid", idOrder );
    }
    COrder::idOrder_t idOrder;
    double dblCommission;
    UpdateCommission( COrder::idOrder_t id, double dblCommission_ )
      : idOrder( id ), dblCommission( dblCommission_ ) {};
  };
}

void COrderManager::ReportCommission( idOrder_t nOrderId, double dblCommission ) {
  try {
    mapOrders_t::iterator iter = LocateOrder( nOrderId );
    pOrder_t pOrder = iter->second.pOrder;
    pOrder->SetCommission( dblCommission );
    if ( 0 != m_pDbSession ) {
      OrderManagerQueries::UpdateCommission 
        commission( pOrder->GetOrderId(), pOrder->GetRow().dblCommission );
      ou::db::QueryFields<OrderManagerQueries::UpdateCommission>::pQueryFields_t pQuery
        = m_pDbSession->SQL<OrderManagerQueries::UpdateCommission>( // todo:  cache this query
          "update orders set commission=?", commission ).Where( "orderid=?" );
    }
  }
  catch (...) {
    std::cout << "Problems in COrderManager::ReportCommission" << std::endl;
  }
}

namespace OrderManagerQueries {
  struct UpdateOrder {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "orderstatus", eOrderStatus );
      ou::db::Field( a, "quantityremaining", nQuantityRemaining );
      ou::db::Field( a, "quantityfilled", nQuantityFilled );
      ou::db::Field( a, "averagefillprice", dblAverageFillPrice );
      ou::db::Field( a, "orderid", idOrder );
    }
    OrderStatus::enumOrderStatus eOrderStatus;
    boost::uint32_t nQuantityRemaining;
    boost::uint32_t nQuantityFilled;
    double dblAverageFillPrice;
    COrder::idOrder_t idOrder;
    UpdateOrder( COrder::idOrder_t idOrder_, OrderStatus::enumOrderStatus eOrderStatus_, 
      boost::uint32_t nQuantityRemaining_, boost::uint32_t nQuantityFilled_, double dblAverageFillPrice_ )
      : idOrder( idOrder_ ), eOrderStatus( eOrderStatus_ ), 
      nQuantityRemaining( nQuantityRemaining_ ), nQuantityFilled( nQuantityFilled_ ), 
      dblAverageFillPrice( dblAverageFillPrice_ ) {};
  };
}

void COrderManager::ReportExecution( idOrder_t nOrderId, const CExecution& exec) { 
  try {
    mapOrders_t::iterator iter = LocateOrder( nOrderId );
    pOrder_t pOrder = iter->second.pOrder;
    OrderStatus::enumOrderStatus status = pOrder->ReportExecution( exec );
    if ( 0 != m_pDbSession ) {
      const COrder::TableRowDef& row( pOrder->GetRow() );
      OrderManagerQueries::UpdateOrder 
        order( nOrderId, row.eOrderStatus, row.nQuantityRemaining, row.nQuantityFilled, row.dblAverageFillPrice );
      ou::db::QueryFields<OrderManagerQueries::UpdateOrder>::pQueryFields_t pQuery
        = m_pDbSession->SQL<OrderManagerQueries::UpdateOrder>( // todo:  cache this query
        "update orders set orderstatus=?, quantityremaining=?, quantityfilled=?, averagefillprice=?", order ).Where( "orderid=?" );
      // add execution record
      pExecution_t pExecution( new CExecution( exec ) );
      pExecution->SetOrderId( nOrderId );
      ou::db::QueryFields<CExecution::TableRowDefNoKey>::pQueryFields_t pQueryExecutionWrite
        = m_pDbSession->Insert<CExecution::TableRowDefNoKey>( 
          const_cast<CExecution::TableRowDefNoKey&>( dynamic_cast<const CExecution::TableRowDefNoKey&>( pExecution->GetRow() ) ) );
      idExecution_t idExecution = m_pDbSession->GetLastRowId();
      pairExecution_t pair( idExecution, pExecution );
      iter->second.pmapExecutions->insert( pair );
    }
//    switch ( status ) {
//      case OrderStatus::Filled:
        //MoveActiveOrderToCompleted( nOrderId );
//        break;
//    }
  }
  catch (...) {
    std::cout << "Problems in COrderManager::ReportExecution" << std::endl;
  }
}

namespace OrderManagerQueries {
  struct UpdateOnOrderError {
    template<class A>
    void Fields( A& a ) {
      ou::db::Field( a, "orderstatus", eOrderStatus );
      ou::db::Field( a, "datetimeclosed", dtOrderClosed );
      ou::db::Field( a, "orderid", idOrder );
    }
    COrder::idOrder_t idOrder;
    ptime dtOrderClosed;
    OrderStatus::enumOrderStatus eOrderStatus;
    UpdateOnOrderError( COrder::idOrder_t id, OrderStatus::enumOrderStatus status, ptime dtOrderClosed_ )
      : idOrder( id ), dtOrderClosed( dtOrderClosed_ ), eOrderStatus( status ) {};
  };
}

void COrderManager::ReportErrors( idOrder_t nOrderId, OrderErrors::enumOrderErrors eError) {
  try {
    mapOrders_t::iterator iter = LocateOrder( nOrderId );
    pOrder_t pOrder = iter->second.pOrder;
    pOrder->ActOnError( eError );
    //MoveActiveOrderToCompleted( nOrderId );
    if ( 0 != m_pDbSession ) {
      OrderManagerQueries::UpdateOnOrderError 
        error( pOrder->GetOrderId(), pOrder->GetRow().eOrderStatus, pOrder->GetRow().dtOrderClosed );
      ou::db::QueryFields<OrderManagerQueries::UpdateOnOrderError>::pQueryFields_t pQuery
        = m_pDbSession->SQL<OrderManagerQueries::UpdateOnOrderError>( // todo:  cache this query
          "update orders set orderstatus=?, datetimeclosed=?", error ).Where( "orderid=?" );
    }
  }
  catch (...) {
    std::cout << "Problems in COrderManager::ReportErrors" << std::endl;
  }
}

void COrderManager::RegisterTablesForCreation( void ) {
  m_pDbSession->RegisterTable<COrder::TableCreateDef>( tablenames::sOrder );
  m_pDbSession->RegisterTable<CExecution::TableCreateDef>( tablenames::sExecution );
}

void COrderManager::RegisterRowDefinitions( void ) {
  m_pDbSession->MapRowDefToTableName<COrder::TableRowDef>( tablenames::sOrder );
  m_pDbSession->MapRowDefToTableName<CExecution::TableRowDef>( tablenames::sExecution );
}

void COrderManager::PopulateTables( void ) {
}

} // namespace tf
} // namespace ou
