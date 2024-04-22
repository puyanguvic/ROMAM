/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "ospf-at-romam-helper.h"
#include "ns3/romam-module.h"
#include "ns3/ipv4-list-routing.h"
#include "ns3/traffic-control-layer.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RomamRoutingHelper");

RomamRoutingHelper::RomamRoutingHelper()
{
}

RomamRoutingHelper::RomamRoutingHelper (const RomamRoutingHelper &o)
{
}

RomamRoutingHelper*
RomamRoutingHelper::Copy (void) const
{
  return new RomamRoutingHelper (*this);
}

Ptr<Ipv4RoutingProtocol>
RomamRoutingHelper::Create (Ptr<Node> node) const
{
  NS_LOG_LOGIC ("Adding DGRRouter interface to node " <<
                node->GetId ());
  // install DGRv2 Queue to netdevices

  // install DGR router to node.
  Ptr<RomamRouter> router = CreateObject<RomamRouter> ();
  node->AggregateObject (router);

  NS_LOG_LOGIC ("Adding RomamRouting Protocol to node " << node->GetId ());
  Ptr<RomamRouting> routing = CreateObject<RomamRouting> ();
  router->SetRoutingProtocol (routing);

  return routing;
}

void 
RomamRoutingHelper::PopulateRoutingTables (void)
{
  std::clock_t t;
  t = clock();
  RouterManager::BuildLSDB ();
  RouterManager::InitializeRoutes ();
  t = clock() - t;
  uint32_t time_init_ms = 1000000.0 * t / CLOCKS_PER_SEC;
  std::cout << "CPU time used for Romam Routing Protocol Init: " << time_init_ms << " ms\n";
}

void 
RomamRoutingHelper::RecomputeRoutingTables (void)
{
  RouterManager::DeleteRoutes ();
  RouterManager::BuildLSDB ();
  RouterManager::InitializeRoutes ();
}

} // namespace ns3