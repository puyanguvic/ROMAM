/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include "routing-algorithm.h"

#include "ns3/assert.h"
#include "ns3/log.h"

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RoutingAlgorithm");

RoutingAlgorithm::~RoutingAlgorithm()
{
    NS_LOG_LOGIC(this);
}

} // namespace ns3
