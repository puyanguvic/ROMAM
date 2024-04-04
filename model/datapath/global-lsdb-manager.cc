/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <utility>
#include <vector>
#include <queue>
#include <algorithm>
#include <iostream>
#include "ns3/assert.h"
#include "ns3/fatal-error.h"
#include "ns3/log.h"
#include "ns3/node-list.h"
#include "ns3/ipv4.h"
#include "ns3/ipv4-routing-protocol.h"
#include "ns3/ipv4-list-routing.h"

#include "global-lsdb-manager.h"
#include "lsa.h"
#include "../utility/romam-router.h"
#include "../romam-routing.h"

#include <ctime>
#include <chrono>

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("GlobalLSDBManager");

// ---------------------------------------------------------------------------
//
// GlobalLSDBManager Implementation
//
// ---------------------------------------------------------------------------

GlobalLSDBManager::GlobalLSDBManager () 
  :
    m_spfroot (0)
{
  NS_LOG_FUNCTION (this);
  m_lsdb = new LSDB ();
}

GlobalLSDBManager::~GlobalLSDBManager ()
{
  NS_LOG_FUNCTION (this);
  if (m_lsdb)
    {
      delete m_lsdb;
    }
}

void
GlobalLSDBManager::DeleteRoutes ()
{
  NS_LOG_FUNCTION (this);
  NodeList::Iterator listEnd = NodeList::End ();
  for (NodeList::Iterator i = NodeList::Begin (); i != listEnd; i++)
    {
      Ptr<Node> node = *i;
      Ptr<RomamRouter> router = node->GetObject<RomamRouter> ();
      if (!router)
        {
          continue;
        }
      Ptr<RomamRouting> gr = router->GetRoutingProtocol ();
      uint32_t j = 0;
      uint32_t nRoutes = gr->GetNRoutes ();
      NS_LOG_LOGIC ("Deleting " << gr->GetNRoutes ()<< " routes from node " << node->GetId ());
      // Each time we delete route 0, the route index shifts downward
      // We can delete all routes if we delete the route numbered 0
      // nRoutes times
      for (j = 0; j < nRoutes; j++)
        {
          NS_LOG_LOGIC ("Deleting global route " << j << " from node " << node->GetId ());
          gr->RemoveRoute (0);
        }
      NS_LOG_LOGIC ("Deleted " << j << " global routes from node "<< node->GetId ());
    }
  if (m_lsdb)
    {
      NS_LOG_LOGIC ("Deleting LSDB, creating new one");
      delete m_lsdb;
      m_lsdb = new LSDB ();
    }
}

//
// In order to build the routing database, we need to walk the list of nodes
// in the system and look for those that support the RomamRouter interface.
// These routers will export a number of Link State Advertisements (LSAs)
// that describe the links and networks that are "adjacent" (i.e., that are
// on the other side of a point-to-point link).  We take these LSAs and put
// add them to the Link State DataBase (LSDB) from which the routes will 
// ultimately be computed.
//
void
GlobalLSDBManager::BuildRoutingDatabase () 
{
  NS_LOG_FUNCTION (this);
//
// Walk the list of nodes looking for the RomamRouter Interface.  Nodes with
// global router interfaces are, not too surprisingly, our routers.
//
  NodeList::Iterator listEnd = NodeList::End ();
  for (NodeList::Iterator i = NodeList::Begin (); i != listEnd; i++)
    {
      Ptr<Node> node = *i;
      Ptr<RomamRouter> rtr = node->GetObject<RomamRouter> ();
//
// Ignore nodes that aren't participating in routing.
//
      if (!rtr)
        {
          continue;
        }
//
// You must call DiscoverLSAs () before trying to use any routing info or to
// update LSAs.  DiscoverLSAs () drives the process of discovering routes in
// the RomamRouter.  Afterward, you may use GetNumLSAs (), which is a very
// computationally inexpensive call.  If you call GetNumLSAs () before calling 
// DiscoverLSAs () will get zero as the number since no routes have been 
// found.
//
      Ptr<RomamRouting> grouting = rtr->GetRoutingProtocol ();
      uint32_t numLSAs = rtr->DiscoverLSAs ();
      NS_LOG_LOGIC ("Found " << numLSAs << " LSAs");

      for (uint32_t j = 0; j < numLSAs; ++j)
        {
          LSA* lsa = new LSA ();
//
// This is the call to actually fetch a Link State Advertisement from the 
// router.
//
          rtr->GetLSA (j, *lsa);
          NS_LOG_LOGIC (*lsa);
//
// Write the newly discovered link state advertisement to the database.
//
          m_lsdb->Insert (lsa->GetLinkStateId (), lsa); 
        }
    }
}

} // namespace ns3
