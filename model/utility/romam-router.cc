
#include "romam-router.h"

#include "../datapath/lsa.h"
#include "../routing_algorithm/dijkstra-route-info-entry.h"
#include "route-manager.h"

#include "ns3/abort.h"
#include "ns3/assert.h"
#include "ns3/bridge-net-device.h"
#include "ns3/channel.h"
#include "ns3/ipv4.h"
#include "ns3/log.h"
#include "ns3/loopback-net-device.h"
#include "ns3/net-device.h"
#include "ns3/node-list.h"
#include "ns3/node.h"

#include <vector>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("RomamRouter");

NS_OBJECT_ENSURE_REGISTERED(RomamRouter);

TypeId
RomamRouter::GetTypeId()
{
    static TypeId tid = TypeId("ns3::RomamRouter").SetParent<Object>().SetGroupName("Romam");
    return tid;
}

RomamRouter::RomamRouter()
    : m_LSAs()
{
    NS_LOG_FUNCTION(this);
    m_routerId.Set(RouteManager::AllocateRouterId());
}

RomamRouter::~RomamRouter()
{
    NS_LOG_FUNCTION(this);
    ClearLSAs();
}

void
RomamRouter::DoDispose()
{
    // TODO: need to be override in the subclasses
    NS_LOG_FUNCTION(this);
    for (auto k = m_injectedRoutes.begin(); k != m_injectedRoutes.end();
         k = m_injectedRoutes.erase(k))
    {
        delete (*k);
    }
    Object::DoDispose();
}

void
RomamRouter::ClearLSAs()
{
    NS_LOG_FUNCTION(this);
    for (auto i = m_LSAs.begin(); i != m_LSAs.end(); i++)
    {
        NS_LOG_LOGIC("Free LSA");

        LSA* p = *i;
        delete p;
        p = nullptr;

        *i = nullptr;
    }
    NS_LOG_LOGIC("Clear list of LSAs");
    m_LSAs.clear();
}

Ipv4Address
RomamRouter::GetRouterId() const
{
    NS_LOG_FUNCTION(this);
    return m_routerId;
}

//
// DiscoverLSAs is called on all nodes in the system that have a RomamRouter
// interface aggregated.  We need to go out and discover any adjacent routers
// and build the Link State Advertisements that reflect them and their associated
// networks.
//
uint32_t
RomamRouter::DiscoverLSAs()
{
    NS_LOG_FUNCTION(this);
    Ptr<Node> node = GetObject<Node>();
    NS_ABORT_MSG_UNLESS(node,
                        "RomamRouter::DiscoverLSAs (): GetObject for <Node> interface failed");
    NS_LOG_LOGIC("For node " << node->GetId());

    ClearLSAs();

    //
    // While building the Router-LSA, keep a list of those NetDevices for
    // which the current node is the designated router and we will later build
    // a NetworkLSA for.
    //
    NetDeviceContainer c;

    //
    // We're aggregated to a node.  We need to ask the node for a pointer to its
    // Ipv4 interface.  This is where the information regarding the attached
    // interfaces lives.  If we're a router, we had better have an Ipv4 interface.
    //
    Ptr<Ipv4> ipv4Local = node->GetObject<Ipv4>();
    NS_ABORT_MSG_UNLESS(ipv4Local,
                        "RomamRouter::DiscoverLSAs (): GetObject for <Ipv4> interface failed");

    //
    // Every router node originates a Router-LSA
    //
    auto pLSA = new LSA;
    pLSA->SetLSType(LSA::RouterLSA);
    pLSA->SetLinkStateId(m_routerId);
    pLSA->SetAdvertisingRouter(m_routerId);
    pLSA->SetStatus(LSA::LSA_SPF_NOT_EXPLORED);
    pLSA->SetNode(node);

    //
    // Ask the node for the number of net devices attached. This isn't necessarily
    // equal to the number of links to adjacent nodes (other routers) as the number
    // of devices may include those for stub networks (e.g., ethernets, etc.) and
    // bridge devices also take up an "extra" net device.
    //
    uint32_t numDevices = node->GetNDevices();

    //
    // Iterate through the devices on the node and walk the channel to see what's
    // on the other side of the standalone devices..
    //
    for (uint32_t i = 0; i < numDevices; ++i)
    {
        Ptr<NetDevice> ndLocal = node->GetDevice(i);

        if (DynamicCast<LoopbackNetDevice>(ndLocal))
        {
            continue;
        }

        //
        // There is an assumption that bridge ports must never have an IP address
        // associated with them.  This turns out to be a very convenient place to
        // check and make sure that this is the case.
        //
        if (NetDeviceIsBridged(ndLocal))
        {
            int32_t ifIndex = ipv4Local->GetInterfaceForDevice(ndLocal);
            NS_ABORT_MSG_IF(
                ifIndex != -1,
                "RomamRouter::DiscoverLSAs(): Bridge ports must not have an IPv4 interface index");
        }

        //
        // Check to see if the net device we just got has a corresponding IP
        // interface (could be a pure L2 NetDevice) -- for example a net device
        // associated with a bridge.  We are only going to involve devices with
        // IP addresses in routing.
        //
        int32_t interfaceNumber = ipv4Local->GetInterfaceForDevice(ndLocal);
        if (interfaceNumber == -1 ||
            !(ipv4Local->IsUp(interfaceNumber) && ipv4Local->IsForwarding(interfaceNumber)))
        {
            NS_LOG_LOGIC("Net device "
                         << ndLocal
                         << "has no IP interface or is not enabled for forwarding, skipping");
            continue;
        }

        //
        // We have a net device that we need to check out.  If it supports
        // broadcast and is not a point-point link, then it will be either a stub
        // network or a transit network depending on the number of routers on
        // the segment.  We add the appropriate link record to the LSA.
        //
        // If the device is a point to point link, we treat it separately.  In
        // that case, there may be zero, one, or two link records added.
        //

        if (ndLocal->IsBroadcast() && !ndLocal->IsPointToPoint())
        {
            NS_LOG_LOGIC("Broadcast link");
            ProcessBroadcastLink(ndLocal, pLSA, c);
        }
        else if (ndLocal->IsPointToPoint())
        {
            NS_LOG_LOGIC("Point=to-point link");
            ProcessPointToPointLink(ndLocal, pLSA);
        }
        else
        {
            NS_ASSERT_MSG(0, "RomamRouter::DiscoverLSAs (): unknown link type");
        }
    }

    NS_LOG_LOGIC("========== LSA for node " << node->GetId() << " ==========");
    NS_LOG_LOGIC(*pLSA);
    m_LSAs.push_back(pLSA);
    pLSA = nullptr;

    //
    // Now, determine whether we need to build a NetworkLSA.  This is the case if
    // we found at least one designated router.
    //
    uint32_t nDesignatedRouters = c.GetN();
    if (nDesignatedRouters > 0)
    {
        NS_LOG_LOGIC("Build Network LSAs");
        BuildNetworkLSAs(c);
    }

    //
    // Build injected route LSAs as external routes
    // RFC 2328, section 12.4.4
    //
    for (auto i = m_injectedRoutes.begin(); i != m_injectedRoutes.end(); i++)
    {
        auto pLSA = new LSA;
        pLSA->SetLSType(LSA::ASExternalLSAs);
        pLSA->SetLinkStateId((*i)->GetDestNetwork());
        pLSA->SetAdvertisingRouter(m_routerId);
        pLSA->SetNetworkLSANetworkMask((*i)->GetDestNetworkMask());
        pLSA->SetStatus(LSA::LSA_SPF_NOT_EXPLORED);
        m_LSAs.push_back(pLSA);
    }
    return m_LSAs.size();
}

void
RomamRouter::ProcessBroadcastLink(Ptr<NetDevice> nd, LSA* pLSA, NetDeviceContainer& c)
{
    NS_LOG_FUNCTION(this << nd << pLSA << &c);

    if (nd->IsBridge())
    {
        ProcessBridgedBroadcastLink(nd, pLSA, c);
    }
    else
    {
        ProcessSingleBroadcastLink(nd, pLSA, c);
    }
}

void
RomamRouter::ProcessSingleBroadcastLink(Ptr<NetDevice> nd, LSA* pLSA, NetDeviceContainer& c)
{
    NS_LOG_FUNCTION(this << nd << pLSA << &c);

    auto plr = new LinkRecord;
    NS_ABORT_MSG_IF(plr == nullptr,
                    "RomamRouter::ProcessSingleBroadcastLink(): Can't alloc link record");

    //
    // We have some preliminaries to do to get enough information to proceed.
    // This information we need comes from the internet stack, so notice that
    // there is an implied assumption that global routing is only going to
    // work with devices attached to the internet stack (have an ipv4 interface
    // associated to them.
    //
    Ptr<Node> node = nd->GetNode();

    Ptr<Ipv4> ipv4Local = node->GetObject<Ipv4>();
    NS_ABORT_MSG_UNLESS(
        ipv4Local,
        "RomamRouter::ProcessSingleBroadcastLink (): GetObject for <Ipv4> interface failed");

    int32_t interfaceLocal = ipv4Local->GetInterfaceForDevice(nd);
    NS_ABORT_MSG_IF(
        interfaceLocal == -1,
        "RomamRouter::ProcessSingleBroadcastLink(): No interface index associated with device");

    if (ipv4Local->GetNAddresses(interfaceLocal) > 1)
    {
        NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the primary one");
    }
    Ipv4Address addrLocal = ipv4Local->GetAddress(interfaceLocal, 0).GetLocal();
    Ipv4Mask maskLocal = ipv4Local->GetAddress(interfaceLocal, 0).GetMask();
    NS_LOG_LOGIC("Working with local address " << addrLocal);
    uint16_t metricLocal = ipv4Local->GetMetric(interfaceLocal);

    //
    // Check to see if the net device is connected to a channel/network that has
    // another router on it.  If there is no other router on the link (but us) then
    // this is a stub network.  If we find another router, then what we have here
    // is a transit network.
    //
    ClearBridgesVisited();
    if (!AnotherRouterOnLink(nd))
    {
        //
        // This is a net device connected to a stub network
        //
        NS_LOG_LOGIC("Router-LSA Stub Network");
        plr->SetLinkType(LinkRecord::StubNetwork);

        //
        // According to OSPF, the Link ID is the IP network number of
        // the attached network.
        //
        plr->SetLinkId(addrLocal.CombineMask(maskLocal));

        //
        // and the Link Data is the network mask; converted to Ipv4Address
        //
        Ipv4Address maskLocalAddr;
        maskLocalAddr.Set(maskLocal.Get());
        plr->SetLinkData(maskLocalAddr);
        plr->SetMetric(metricLocal);
        pLSA->AddLinkRecord(plr);
        plr = nullptr;
    }
    else
    {
        //
        // We have multiple routers on a broadcast interface, so this is
        // a transit network.
        //
        NS_LOG_LOGIC("Router-LSA Transit Network");
        plr->SetLinkType(LinkRecord::TransitNetwork);

        //
        // By definition, the router with the lowest IP address is the
        // designated router for the network.  OSPF says that the Link ID
        // gets the IP interface address of the designated router in this
        // case.
        //
        ClearBridgesVisited();
        Ipv4Address designatedRtr;
        designatedRtr = FindDesignatedRouterForLink(nd);

        //
        // Let's double-check that any designated router we find out on our
        // network is really on our network.
        //
        if (designatedRtr != "255.255.255.255")
        {
            Ipv4Address networkHere = addrLocal.CombineMask(maskLocal);
            Ipv4Address networkThere = designatedRtr.CombineMask(maskLocal);
            NS_ABORT_MSG_UNLESS(
                networkHere == networkThere,
                "RomamRouter::ProcessSingleBroadcastLink(): Network number confusion ("
                    << addrLocal << "/" << maskLocal.GetPrefixLength() << ", " << designatedRtr
                    << "/" << maskLocal.GetPrefixLength() << ")");
        }
        if (designatedRtr == addrLocal)
        {
            c.Add(nd);
            NS_LOG_LOGIC("Node " << node->GetId() << " elected a designated router");
        }
        plr->SetLinkId(designatedRtr);

        //
        // OSPF says that the Link Data is this router's own IP address.
        //
        plr->SetLinkData(addrLocal);
        plr->SetMetric(metricLocal);
        pLSA->AddLinkRecord(plr);
        plr = nullptr;
    }
}

void
RomamRouter::ProcessBridgedBroadcastLink(Ptr<NetDevice> nd, LSA* pLSA, NetDeviceContainer& c)
{
    NS_LOG_FUNCTION(this << nd << pLSA << &c);
    NS_ASSERT_MSG(nd->IsBridge(),
                  "RomamRouter::ProcessBridgedBroadcastLink(): Called with non-bridge net device");

#if 0
  //
  // It is possible to admit the possibility that a bridge device on a node
  // can also participate in routing.  This would surprise people who don't
  // come from Microsoft-land where they do use such a construct.  Based on
  // the principle of least-surprise, we will leave the relatively simple
  // code in place to do this, but not enable it until someone really wants
  // the capability.  Even then, we will not enable this code as a default
  // but rather something you will have to go and turn on.
  //

  Ptr<BridgeNetDevice> bnd = nd->GetObject<BridgeNetDevice> ();
  NS_ABORT_MSG_UNLESS (bnd, "RomamRouter::DiscoverLSAs (): GetObject for <BridgeNetDevice> failed");

  //
  // We have some preliminaries to do to get enough information to proceed.
  // This information we need comes from the internet stack, so notice that
  // there is an implied assumption that global routing is only going to
  // work with devices attached to the internet stack (have an ipv4 interface
  // associated to them.
  //
  Ptr<Node> node = nd->GetNode ();
  Ptr<Ipv4> ipv4Local = node->GetObject<Ipv4> ();
  NS_ABORT_MSG_UNLESS (ipv4Local, "RomamRouter::ProcessBridgedBroadcastLink (): GetObject for <Ipv4> interface failed");

  int32_t interfaceLocal = ipv4Local->GetInterfaceForDevice (nd);
  NS_ABORT_MSG_IF (interfaceLocal == -1, "RomamRouter::ProcessBridgedBroadcastLink(): No interface index associated with device");

  if (ipv4Local->GetNAddresses (interfaceLocal) > 1)
    {
      NS_LOG_WARN ("Warning, interface has multiple IP addresses; using only the primary one");
    }
  Ipv4Address addrLocal = ipv4Local->GetAddress (interfaceLocal, 0).GetLocal ();
  Ipv4Mask maskLocal = ipv4Local->GetAddress (interfaceLocal, 0).GetMask ();
  NS_LOG_LOGIC ("Working with local address " << addrLocal);
  uint16_t metricLocal = ipv4Local->GetMetric (interfaceLocal);

  //
  // We need to handle a bridge on the router.  This means that we have been
  // given a net device that is a BridgeNetDevice.  It has an associated Ipv4
  // interface index and address.  Some number of other net devices live "under"
  // the bridge device as so-called bridge ports.  In a nutshell, what we have
  // to do is to repeat what is done for a single broadcast link on all of
  // those net devices living under the bridge (trolls?)
  //

  bool areTransitNetwork = false;
  Ipv4Address designatedRtr ("255.255.255.255");

  for (uint32_t i = 0; i < bnd->GetNBridgePorts (); ++i)
    {
      Ptr<NetDevice> ndTemp = bnd->GetBridgePort (i);

      //
      // We have to decide if we are a transit network.  This is characterized
      // by the presence of another router on the network segment.  If we find
      // another router on any of our bridged links, we are a transit network.
      //
      ClearBridgesVisited ();
      if (AnotherRouterOnLink (ndTemp))
        {
          areTransitNetwork = true;

          //
          // If we're going to be a transit network, then we have got to elect
          // a designated router for the whole bridge.  This means finding the
          // router with the lowest IP address on the whole bridge.  We ask
          // for the lowest address on each segment and pick the lowest of them
          // all.
          //
          ClearBridgesVisited ();
          Ipv4Address designatedRtrTemp = FindDesignatedRouterForLink (ndTemp);

          //
          // Let's double-check that any designated router we find out on our
          // network is really on our network.
          //
          if (designatedRtrTemp != "255.255.255.255")
            {
              Ipv4Address networkHere = addrLocal.CombineMask (maskLocal);
              Ipv4Address networkThere = designatedRtrTemp.CombineMask (maskLocal);
              NS_ABORT_MSG_UNLESS (networkHere == networkThere,
                                   "RomamRouter::ProcessSingleBroadcastLink(): Network number confusion (" <<
                                   addrLocal << "/" << maskLocal.GetPrefixLength () << ", " <<
                                   designatedRtrTemp << "/" << maskLocal.GetPrefixLength () << ")");
            }
          if (designatedRtrTemp < designatedRtr)
            {
              designatedRtr = designatedRtrTemp;
            }
        }
    }
  //
  // That's all the information we need to put it all together, just like we did
  // in the case of a single broadcast link.
  //

  GlobalRoutingLinkRecord *plr = new GlobalRoutingLinkRecord;
  NS_ABORT_MSG_IF (plr == 0, "RomamRouter::ProcessBridgedBroadcastLink(): Can't alloc link record");

  if (areTransitNetwork == false)
    {
      //
      // This is a net device connected to a bridge of stub networks
      //
      NS_LOG_LOGIC ("Router-LSA Stub Network");
      plr->SetLinkType (GlobalRoutingLinkRecord::StubNetwork);

      //
      // According to OSPF, the Link ID is the IP network number of
      // the attached network.
      //
      plr->SetLinkId (addrLocal.CombineMask (maskLocal));

      //
      // and the Link Data is the network mask; converted to Ipv4Address
      //
      Ipv4Address maskLocalAddr;
      maskLocalAddr.Set (maskLocal.Get ());
      plr->SetLinkData (maskLocalAddr);
      plr->SetMetric (metricLocal);
      pLSA->AddLinkRecord (plr);
      plr = 0;
    }
  else
    {
      //
      // We have multiple routers on a bridged broadcast interface, so this is
      // a transit network.
      //
      NS_LOG_LOGIC ("Router-LSA Transit Network");
      plr->SetLinkType (GlobalRoutingLinkRecord::TransitNetwork);

      //
      // By definition, the router with the lowest IP address is the
      // designated router for the network.  OSPF says that the Link ID
      // gets the IP interface address of the designated router in this
      // case.
      //
      if (designatedRtr == addrLocal)
        {
          c.Add (nd);
          NS_LOG_LOGIC ("Node " << node->GetId () << " elected a designated router");
        }
      plr->SetLinkId (designatedRtr);

      //
      // OSPF says that the Link Data is this router's own IP address.
      //
      plr->SetLinkData (addrLocal);
      plr->SetMetric (metricLocal);
      pLSA->AddLinkRecord (plr);
      plr = 0;
    }
#endif
}

void
RomamRouter::ProcessPointToPointLink(Ptr<NetDevice> ndLocal, LSA* pLSA)
{
    NS_LOG_FUNCTION(this << ndLocal << pLSA);

    //
    // We have some preliminaries to do to get enough information to proceed.
    // This information we need comes from the internet stack, so notice that
    // there is an implied assumption that global routing is only going to
    // work with devices attached to the internet stack (have an ipv4 interface
    // associated to them.
    //
    Ptr<Node> nodeLocal = ndLocal->GetNode();

    Ptr<Ipv4> ipv4Local = nodeLocal->GetObject<Ipv4>();
    NS_ABORT_MSG_UNLESS(
        ipv4Local,
        "RomamRouter::ProcessPointToPointLink (): GetObject for <Ipv4> interface failed");

    int32_t interfaceLocal = ipv4Local->GetInterfaceForDevice(ndLocal);
    NS_ABORT_MSG_IF(
        interfaceLocal == -1,
        "RomamRouter::ProcessPointToPointLink (): No interface index associated with device");

    if (ipv4Local->GetNAddresses(interfaceLocal) > 1)
    {
        NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the primary one");
    }
    Ipv4Address addrLocal = ipv4Local->GetAddress(interfaceLocal, 0).GetLocal();
    NS_LOG_LOGIC("Working with local address " << addrLocal);
    uint16_t metricLocal = ipv4Local->GetMetric(interfaceLocal);

    //
    // Now, we're going to walk over to the remote net device on the other end of
    // the point-to-point channel we know we have.  This is where our adjacent
    // router (to use OSPF lingo) is running.
    //
    Ptr<Channel> ch = ndLocal->GetChannel();

    //
    // Get the net device on the other side of the point-to-point channel.
    //
    Ptr<NetDevice> ndRemote = GetAdjacent(ndLocal, ch);

    //
    // The adjacent net device is aggregated to a node.  We need to ask that net
    // device for its node, then ask that node for its Ipv4 interface.  Note a
    // requirement that nodes on either side of a point-to-point link must have
    // internet stacks; and an assumption that point-to-point links are incompatible
    // with bridging.
    //
    Ptr<Node> nodeRemote = ndRemote->GetNode();
    Ptr<Ipv4> ipv4Remote = nodeRemote->GetObject<Ipv4>();
    NS_ABORT_MSG_UNLESS(
        ipv4Remote,
        "RomamRouter::ProcessPointToPointLink(): GetObject for remote <Ipv4> failed");

    //
    // Further note the requirement that nodes on either side of a point-to-point
    // link must participate in global routing and therefore have a RomamRouter
    // interface aggregated.
    //
    Ptr<RomamRouter> rtrRemote = nodeRemote->GetObject<RomamRouter>();
    if (!rtrRemote)
    {
        // This case is possible if the remote does not participate in global routing
        return;
    }
    //
    // We're going to need the remote router ID, so we might as well get it now.
    //
    Ipv4Address rtrIdRemote = rtrRemote->GetRouterId();
    NS_LOG_LOGIC("Working with remote router " << rtrIdRemote);

    //
    // Now, just like we did above, we need to get the IP interface index for the
    // net device on the other end of the point-to-point channel.
    //
    int32_t interfaceRemote = ipv4Remote->GetInterfaceForDevice(ndRemote);
    NS_ABORT_MSG_IF(interfaceRemote == -1,
                    "RomamRouter::ProcessPointToPointLinks(): No interface index associated with "
                    "remote device");

    //
    // Now that we have the Ipv4 interface, we can get the (remote) address and
    // mask we need.
    //
    if (ipv4Remote->GetNAddresses(interfaceRemote) > 1)
    {
        NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the primary one");
    }
    Ipv4Address addrRemote = ipv4Remote->GetAddress(interfaceRemote, 0).GetLocal();
    Ipv4Mask maskRemote = ipv4Remote->GetAddress(interfaceRemote, 0).GetMask();
    NS_LOG_LOGIC("Working with remote address " << addrRemote);

    //
    // Now we can fill out the link records for this link.  There are always two
    // link records; the first is a point-to-point record describing the link and
    // the second is a stub network record with the network number.
    //
    LinkRecord* plr;
    if (ipv4Remote->IsUp(interfaceRemote))
    {
        NS_LOG_LOGIC("Remote side interface " << interfaceRemote << " is up-- add a type 1 link");

        plr = new LinkRecord;
        NS_ABORT_MSG_IF(plr == nullptr,
                        "RomamRouter::ProcessPointToPointLink(): Can't alloc link record");
        plr->SetLinkType(LinkRecord::PointToPoint);
        plr->SetLinkId(rtrIdRemote);
        plr->SetLinkData(addrLocal);
        plr->SetMetric(metricLocal);
        pLSA->AddLinkRecord(plr);
        plr = nullptr;
    }

    // Regardless of state of peer, add a type 3 link (RFC 2328: 12.4.1.1)
    plr = new LinkRecord;
    NS_ABORT_MSG_IF(plr == nullptr,
                    "RomamRouter::ProcessPointToPointLink(): Can't alloc link record");
    plr->SetLinkType(LinkRecord::StubNetwork);
    plr->SetLinkId(addrRemote);
    plr->SetLinkData(Ipv4Address(maskRemote.Get())); // Frown
    plr->SetMetric(metricLocal);
    pLSA->AddLinkRecord(plr);
    plr = nullptr;
}

void
RomamRouter::BuildNetworkLSAs(NetDeviceContainer c)
{
    NS_LOG_FUNCTION(this << &c);

    uint32_t nDesignatedRouters = c.GetN();
    NS_LOG_DEBUG("Number of designated routers: " << nDesignatedRouters);

    for (uint32_t i = 0; i < nDesignatedRouters; ++i)
    {
        //
        // Build one NetworkLSA for each net device talking to a network that we are the
        // designated router for.  These devices are in the provided container.
        //
        Ptr<NetDevice> ndLocal = c.Get(i);
        Ptr<Node> node = ndLocal->GetNode();

        Ptr<Ipv4> ipv4Local = node->GetObject<Ipv4>();
        NS_ABORT_MSG_UNLESS(
            ipv4Local,
            "RomamRouter::ProcessPointToPointLink (): GetObject for <Ipv4> interface failed");

        int32_t interfaceLocal = ipv4Local->GetInterfaceForDevice(ndLocal);
        NS_ABORT_MSG_IF(
            interfaceLocal == -1,
            "RomamRouter::BuildNetworkLSAs (): No interface index associated with device");

        if (ipv4Local->GetNAddresses(interfaceLocal) > 1)
        {
            NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the primary one");
        }
        Ipv4Address addrLocal = ipv4Local->GetAddress(interfaceLocal, 0).GetLocal();
        Ipv4Mask maskLocal = ipv4Local->GetAddress(interfaceLocal, 0).GetMask();

        auto pLSA = new LSA;
        NS_ABORT_MSG_IF(pLSA == nullptr,
                        "RomamRouter::BuildNetworkLSAs(): Can't alloc link record");

        pLSA->SetLSType(LSA::NetworkLSA);
        pLSA->SetLinkStateId(addrLocal);
        pLSA->SetAdvertisingRouter(m_routerId);
        pLSA->SetNetworkLSANetworkMask(maskLocal);
        pLSA->SetStatus(LSA::LSA_SPF_NOT_EXPLORED);
        pLSA->SetNode(node);

        //
        // Build a list of AttachedRouters by walking the devices in the channel
        // and, if we find a node with a RomamRouter interface and an IPv4
        // interface associated with that device, we call it an attached router.
        //
        ClearBridgesVisited();
        Ptr<Channel> ch = ndLocal->GetChannel();
        std::size_t nDevices = ch->GetNDevices();
        NS_ASSERT(nDevices);
        NetDeviceContainer deviceList = FindAllNonBridgedDevicesOnLink(ch);
        NS_LOG_LOGIC("Found " << deviceList.GetN() << " non-bridged devices on channel");

        for (uint32_t i = 0; i < deviceList.GetN(); i++)
        {
            Ptr<NetDevice> tempNd = deviceList.Get(i);
            NS_ASSERT(tempNd);
            if (tempNd == ndLocal)
            {
                NS_LOG_LOGIC("Adding " << addrLocal << " to Network LSA");
                pLSA->AddAttachedRouter(addrLocal);
                continue;
            }
            Ptr<Node> tempNode = tempNd->GetNode();

            // Does the node in question have a RomamRouter interface?  If not it can
            // hardly be considered an attached router.
            //
            Ptr<RomamRouter> rtr = tempNode->GetObject<RomamRouter>();
            if (!rtr)
            {
                NS_LOG_LOGIC("Node " << tempNode->GetId()
                                     << " does not have RomamRouter interface--skipping");
                continue;
            }

            //
            // Does the attached node have an ipv4 interface for the device we're probing?
            // If not, it can't play router.
            //
            Ptr<Ipv4> tempIpv4 = tempNode->GetObject<Ipv4>();
            int32_t tempInterface = tempIpv4->GetInterfaceForDevice(tempNd);

            if (tempInterface != -1)
            {
                Ptr<Ipv4> tempIpv4 = tempNode->GetObject<Ipv4>();
                NS_ASSERT(tempIpv4);
                if (!tempIpv4->IsUp(tempInterface))
                {
                    NS_LOG_LOGIC("Remote side interface " << tempInterface << " not up");
                }
                else
                {
                    if (tempIpv4->GetNAddresses(tempInterface) > 1)
                    {
                        NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the "
                                    "primary one");
                    }
                    Ipv4Address tempAddr = tempIpv4->GetAddress(tempInterface, 0).GetLocal();
                    NS_LOG_LOGIC("Adding " << tempAddr << " to Network LSA");
                    pLSA->AddAttachedRouter(tempAddr);
                }
            }
            else
            {
                NS_LOG_LOGIC("Node " << tempNode->GetId() << " device " << tempNd
                                     << " does not have IPv4 interface; skipping");
            }
        }
        m_LSAs.push_back(pLSA);
        NS_LOG_LOGIC("========== LSA for node " << node->GetId() << " ==========");
        NS_LOG_LOGIC(*pLSA);
        pLSA = nullptr;
    }
}

NetDeviceContainer
RomamRouter::FindAllNonBridgedDevicesOnLink(Ptr<Channel> ch) const
{
    NS_LOG_FUNCTION(this << ch);
    NetDeviceContainer c;

    for (std::size_t i = 0; i < ch->GetNDevices(); i++)
    {
        Ptr<NetDevice> nd = ch->GetDevice(i);
        NS_LOG_LOGIC("checking to see if the device " << nd << " is bridged");
        Ptr<BridgeNetDevice> bnd = NetDeviceIsBridged(nd);
        if (bnd && !BridgeHasAlreadyBeenVisited(bnd))
        {
            NS_LOG_LOGIC("Device is bridged by BridgeNetDevice "
                         << bnd << " with " << bnd->GetNBridgePorts() << " ports");
            MarkBridgeAsVisited(bnd);
            // Find all channels bridged together, and recursively call
            // on all other channels
            for (uint32_t j = 0; j < bnd->GetNBridgePorts(); j++)
            {
                Ptr<NetDevice> bridgedDevice = bnd->GetBridgePort(j);
                if (bridgedDevice->GetChannel() == ch)
                {
                    NS_LOG_LOGIC("Skipping my own device/channel");
                    continue;
                }
                NS_LOG_LOGIC("Calling on channel " << bridgedDevice->GetChannel());
                c.Add(FindAllNonBridgedDevicesOnLink(bridgedDevice->GetChannel()));
            }
        }
        else
        {
            NS_LOG_LOGIC("Device is not bridged; adding");
            c.Add(nd);
        }
    }
    NS_LOG_LOGIC("Found " << c.GetN() << " devices");
    return c;
}

//
// Given a local net device, we need to walk the channel to which the net device is
// attached and look for nodes with RomamRouter interfaces on them (one of them
// will be us).  Of these, the router with the lowest IP address on the net device
// connecting to the channel becomes the designated router for the link.
//
Ipv4Address
RomamRouter::FindDesignatedRouterForLink(Ptr<NetDevice> ndLocal) const
{
    NS_LOG_FUNCTION(this << ndLocal);

    Ptr<Channel> ch = ndLocal->GetChannel();
    uint32_t nDevices = ch->GetNDevices();
    NS_ASSERT(nDevices);

    NS_LOG_LOGIC("Looking for designated router off of net device " << ndLocal << " on node "
                                                                    << ndLocal->GetNode()->GetId());

    Ipv4Address designatedRtr("255.255.255.255");

    //
    // Look through all of the devices on the channel to which the net device
    // in question is attached.
    //
    for (uint32_t i = 0; i < nDevices; i++)
    {
        Ptr<NetDevice> ndOther = ch->GetDevice(i);
        NS_ASSERT(ndOther);

        Ptr<Node> nodeOther = ndOther->GetNode();

        NS_LOG_LOGIC("Examine channel device " << i << " on node " << nodeOther->GetId());

        //
        // For all other net devices, we need to check and see if a router
        // is present.  If the net device on the other side is a bridged
        // device, we need to consider all of the other devices on the
        // bridge as well (all of the bridge ports.
        //
        NS_LOG_LOGIC("checking to see if the device is bridged");
        Ptr<BridgeNetDevice> bnd = NetDeviceIsBridged(ndOther);
        if (bnd)
        {
            NS_LOG_LOGIC("Device is bridged by BridgeNetDevice " << bnd);

            //
            // When enumerating a bridge, don't count the netdevice we came in on
            //
            if (ndLocal == ndOther)
            {
                NS_LOG_LOGIC("Skip -- it is where we came from.");
                continue;
            }

            //
            // It is possible that the bridge net device is sitting under a
            // router, so we have to check for the presence of that router
            // before we run off and follow all the links
            //
            // We require a designated router to have a RomamRouter interface and
            // an internet stack that includes the Ipv4 interface.  If it doesn't
            // it can't play router.
            //
            NS_LOG_LOGIC("Checking for router on bridge net device " << bnd);
            Ptr<RomamRouter> rtr = nodeOther->GetObject<RomamRouter>();
            Ptr<Ipv4> ipv4 = nodeOther->GetObject<Ipv4>();
            if (rtr && ipv4)
            {
                int32_t interfaceOther = ipv4->GetInterfaceForDevice(bnd);
                if (interfaceOther != -1)
                {
                    NS_LOG_LOGIC("Found router on bridge net device " << bnd);
                    if (!ipv4->IsUp(interfaceOther))
                    {
                        NS_LOG_LOGIC("Remote side interface " << interfaceOther << " not up");
                        continue;
                    }
                    if (ipv4->GetNAddresses(interfaceOther) > 1)
                    {
                        NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the "
                                    "primary one");
                    }
                    Ipv4Address addrOther = ipv4->GetAddress(interfaceOther, 0).GetLocal();
                    designatedRtr = addrOther < designatedRtr ? addrOther : designatedRtr;
                    NS_LOG_LOGIC("designated router now " << designatedRtr);
                }
            }

            //
            // Check if we have seen this bridge net device already while
            // recursively enumerating an L2 broadcast domain. If it is new
            // to us, go ahead and process it. If we have already processed it,
            // move to the next
            //
            if (BridgeHasAlreadyBeenVisited(bnd))
            {
                NS_ABORT_MSG("ERROR: L2 forwarding loop detected!");
            }

            MarkBridgeAsVisited(bnd);

            NS_LOG_LOGIC("Looking through bridge ports of bridge net device " << bnd);
            for (uint32_t j = 0; j < bnd->GetNBridgePorts(); ++j)
            {
                Ptr<NetDevice> ndBridged = bnd->GetBridgePort(j);
                NS_LOG_LOGIC("Examining bridge port " << j << " device " << ndBridged);
                if (ndBridged == ndOther)
                {
                    NS_LOG_LOGIC("That bridge port is me, don't walk backward");
                    continue;
                }

                NS_LOG_LOGIC("Recursively looking for routers down bridge port " << ndBridged);
                Ipv4Address addrOther = FindDesignatedRouterForLink(ndBridged);
                designatedRtr = addrOther < designatedRtr ? addrOther : designatedRtr;
                NS_LOG_LOGIC("designated router now " << designatedRtr);
            }
        }
        else
        {
            NS_LOG_LOGIC("This device is not bridged");
            Ptr<Node> nodeOther = ndOther->GetNode();
            NS_ASSERT(nodeOther);

            //
            // We require a designated router to have a RomamRouter interface and
            // an internet stack that includes the Ipv4 interface.  If it doesn't
            //
            Ptr<RomamRouter> rtr = nodeOther->GetObject<RomamRouter>();
            Ptr<Ipv4> ipv4 = nodeOther->GetObject<Ipv4>();
            if (rtr && ipv4)
            {
                int32_t interfaceOther = ipv4->GetInterfaceForDevice(ndOther);
                if (interfaceOther != -1)
                {
                    if (!ipv4->IsUp(interfaceOther))
                    {
                        NS_LOG_LOGIC("Remote side interface " << interfaceOther << " not up");
                        continue;
                    }
                    NS_LOG_LOGIC("Found router on net device " << ndOther);
                    if (ipv4->GetNAddresses(interfaceOther) > 1)
                    {
                        NS_LOG_WARN("Warning, interface has multiple IP addresses; using only the "
                                    "primary one");
                    }
                    Ipv4Address addrOther = ipv4->GetAddress(interfaceOther, 0).GetLocal();
                    designatedRtr = addrOther < designatedRtr ? addrOther : designatedRtr;
                    NS_LOG_LOGIC("designated router now " << designatedRtr);
                }
            }
        }
    }
    return designatedRtr;
}

//
// Given a node and an attached net device, take a look off in the channel to
// which the net device is attached and look for a node on the other side
// that has a RomamRouter interface aggregated.  Life gets more complicated
// when there is a bridged net device on the other side.
//
bool
RomamRouter::AnotherRouterOnLink(Ptr<NetDevice> nd) const
{
    NS_LOG_FUNCTION(this << nd);

    Ptr<Channel> ch = nd->GetChannel();
    if (!ch)
    {
        // It may be that this net device is a stub device, without a channel
        return false;
    }
    uint32_t nDevices = ch->GetNDevices();
    NS_ASSERT(nDevices);

    NS_LOG_LOGIC("Looking for routers off of net device " << nd << " on node "
                                                          << nd->GetNode()->GetId());

    //
    // Look through all of the devices on the channel to which the net device
    // in question is attached.
    //
    for (uint32_t i = 0; i < nDevices; i++)
    {
        Ptr<NetDevice> ndOther = ch->GetDevice(i);
        NS_ASSERT(ndOther);

        NS_LOG_LOGIC("Examine channel device " << i << " on node " << ndOther->GetNode()->GetId());

        //
        // Ignore the net device itself.
        //
        if (ndOther == nd)
        {
            NS_LOG_LOGIC("Myself, skip");
            continue;
        }

        //
        // For all other net devices, we need to check and see if a router
        // is present.  If the net device on the other side is a bridged
        // device, we need to consider all of the other devices on the
        // bridge.
        //
        NS_LOG_LOGIC("checking to see if device is bridged");
        Ptr<BridgeNetDevice> bnd = NetDeviceIsBridged(ndOther);
        if (bnd)
        {
            NS_LOG_LOGIC("Device is bridged by net device " << bnd);

            //
            // Check if we have seen this bridge net device already while
            // recursively enumerating an L2 broadcast domain. If it is new
            // to us, go ahead and process it. If we have already processed it,
            // move to the next
            //
            if (BridgeHasAlreadyBeenVisited(bnd))
            {
                NS_ABORT_MSG("ERROR: L2 forwarding loop detected!");
            }

            MarkBridgeAsVisited(bnd);

            NS_LOG_LOGIC("Looking through bridge ports of bridge net device " << bnd);
            for (uint32_t j = 0; j < bnd->GetNBridgePorts(); ++j)
            {
                Ptr<NetDevice> ndBridged = bnd->GetBridgePort(j);
                NS_LOG_LOGIC("Examining bridge port " << j << " device " << ndBridged);
                if (ndBridged == ndOther)
                {
                    NS_LOG_LOGIC("That bridge port is me, skip");
                    continue;
                }

                NS_LOG_LOGIC("Recursively looking for routers on bridge port " << ndBridged);
                if (AnotherRouterOnLink(ndBridged))
                {
                    NS_LOG_LOGIC("Found routers on bridge port, return true");
                    return true;
                }
            }
            NS_LOG_LOGIC("No routers on bridged net device, return false");
            return false;
        }

        NS_LOG_LOGIC("This device is not bridged");
        Ptr<Node> nodeTemp = ndOther->GetNode();
        NS_ASSERT(nodeTemp);

        Ptr<RomamRouter> rtr = nodeTemp->GetObject<RomamRouter>();
        if (rtr)
        {
            NS_LOG_LOGIC("Found RomamRouter interface, return true");
            return true;
        }
        else
        {
            NS_LOG_LOGIC("No RomamRouter interface on device, continue search");
        }
    }
    NS_LOG_LOGIC("No routers found, return false");
    return false;
}

uint32_t
RomamRouter::GetNumLSAs() const
{
    NS_LOG_FUNCTION(this);
    return m_LSAs.size();
}

//
// Get the nth link state advertisement from this router.
//
bool
RomamRouter::GetLSA(uint32_t n, LSA& lsa) const
{
    NS_LOG_FUNCTION(this << n << &lsa);
    NS_ASSERT_MSG(lsa.IsEmpty(), "RomamRouter::GetLSA (): Must pass empty LSA");
    //
    // All of the work was done in GetNumLSAs.  All we have to do here is to
    // walk the list of link state advertisements created there and return the
    // one the client is interested in.
    //
    auto i = m_LSAs.begin();
    uint32_t j = 0;

    for (; i != m_LSAs.end(); i++, j++)
    {
        if (j == n)
        {
            LSA* p = *i;
            lsa = *p;
            return true;
        }
    }

    return false;
}

void
RomamRouter::InjectRoute(Ipv4Address network, Ipv4Mask networkMask)
{
    NS_LOG_FUNCTION(this << network << networkMask);
    auto route = new DijkstraRIE();
    //
    // Interface number does not matter here, using 1.
    //
    *route = DijkstraRIE::CreateNetworkRouteTo(network, networkMask, 1);
    m_injectedRoutes.push_back(route);
}

DijkstraRIE*
RomamRouter::GetInjectedRoute(uint32_t index)
{
    NS_LOG_FUNCTION(this << index);
    if (index < m_injectedRoutes.size())
    {
        uint32_t tmp = 0;
        for (auto i = m_injectedRoutes.begin(); i != m_injectedRoutes.end(); i++)
        {
            if (tmp == index)
            {
                return *i;
            }
            tmp++;
        }
    }
    NS_ASSERT(false);
    // quiet compiler.
    return nullptr;
}

uint32_t
RomamRouter::GetNInjectedRoutes()
{
    NS_LOG_FUNCTION(this);
    return m_injectedRoutes.size();
}

void
RomamRouter::RemoveInjectedRoute(uint32_t index)
{
    NS_LOG_FUNCTION(this << index);
    NS_ASSERT(index < m_injectedRoutes.size());
    uint32_t tmp = 0;
    for (auto i = m_injectedRoutes.begin(); i != m_injectedRoutes.end(); i++)
    {
        if (tmp == index)
        {
            NS_LOG_LOGIC("Removing route " << index << "; size = " << m_injectedRoutes.size());
            delete *i;
            m_injectedRoutes.erase(i);
            return;
        }
        tmp++;
    }
}

bool
RomamRouter::WithdrawRoute(Ipv4Address network, Ipv4Mask networkMask)
{
    NS_LOG_FUNCTION(this << network << networkMask);
    for (auto i = m_injectedRoutes.begin(); i != m_injectedRoutes.end(); i++)
    {
        if ((*i)->GetDestNetwork() == network && (*i)->GetDestNetworkMask() == networkMask)
        {
            NS_LOG_LOGIC("Withdrawing route to network/mask " << network << "/" << networkMask);
            delete *i;
            m_injectedRoutes.erase(i);
            return true;
        }
    }
    return false;
}

//
// Link through the given channel and find the net device that's on the
// other end.  This only makes sense with a point-to-point channel.
//
Ptr<NetDevice>
RomamRouter::GetAdjacent(Ptr<NetDevice> nd, Ptr<Channel> ch) const
{
    NS_LOG_FUNCTION(this << nd << ch);
    NS_ASSERT_MSG(ch->GetNDevices() == 2,
                  "RomamRouter::GetAdjacent (): Channel with other than two devices");
    //
    // This is a point to point channel with two endpoints.  Get both of them.
    //
    Ptr<NetDevice> nd1 = ch->GetDevice(0);
    Ptr<NetDevice> nd2 = ch->GetDevice(1);
    //
    // One of the endpoints is going to be "us" -- that is the net device attached
    // to the node on which we're running -- i.e., "nd".  The other endpoint (the
    // one to which we are connected via the channel) is the adjacent router.
    //
    if (nd1 == nd)
    {
        return nd2;
    }
    else if (nd2 == nd)
    {
        return nd1;
    }
    else
    {
        NS_ASSERT_MSG(false, "RomamRouter::GetAdjacent (): Wrong or confused channel?");
        return nullptr;
    }
}

//
// Decide whether or not a given net device is being bridged by a BridgeNetDevice.
//
Ptr<BridgeNetDevice>
RomamRouter::NetDeviceIsBridged(Ptr<NetDevice> nd) const
{
    NS_LOG_FUNCTION(this << nd);

    Ptr<Node> node = nd->GetNode();
    uint32_t nDevices = node->GetNDevices();

    //
    // There is no bit on a net device that says it is being bridged, so we have
    // to look for bridges on the node to which the device is attached.  If we
    // find a bridge, we need to look through its bridge ports (the devices it
    // bridges) to see if we find the device in question.
    //
    for (uint32_t i = 0; i < nDevices; ++i)
    {
        Ptr<NetDevice> ndTest = node->GetDevice(i);
        NS_LOG_LOGIC("Examine device " << i << " " << ndTest);

        if (ndTest->IsBridge())
        {
            NS_LOG_LOGIC("device " << i << " is a bridge net device");
            Ptr<BridgeNetDevice> bnd = ndTest->GetObject<BridgeNetDevice>();
            NS_ABORT_MSG_UNLESS(
                bnd,
                "RomamRouter::DiscoverLSAs (): GetObject for <BridgeNetDevice> failed");

            for (uint32_t j = 0; j < bnd->GetNBridgePorts(); ++j)
            {
                NS_LOG_LOGIC("Examine bridge port " << j << " " << bnd->GetBridgePort(j));
                if (bnd->GetBridgePort(j) == nd)
                {
                    NS_LOG_LOGIC("Net device " << nd << " is bridged by " << bnd);
                    return bnd;
                }
            }
        }
    }
    NS_LOG_LOGIC("Net device " << nd << " is not bridged");
    return nullptr;
}

//
// Start a new enumeration of an L2 broadcast domain by clearing m_bridgesVisited
//
void
RomamRouter::ClearBridgesVisited() const
{
    m_bridgesVisited.clear();
}

//
// Check if we have already visited a given bridge net device by searching m_bridgesVisited
//
bool
RomamRouter::BridgeHasAlreadyBeenVisited(Ptr<BridgeNetDevice> bridgeNetDevice) const
{
    for (auto iter = m_bridgesVisited.begin(); iter != m_bridgesVisited.end(); ++iter)
    {
        if (bridgeNetDevice == *iter)
        {
            NS_LOG_LOGIC("Bridge " << bridgeNetDevice << " has been visited.");
            return true;
        }
    }
    return false;
}

//
// Remember that we visited a bridge net device by adding it to m_bridgesVisited
//
void
RomamRouter::MarkBridgeAsVisited(Ptr<BridgeNetDevice> bridgeNetDevice) const
{
    NS_LOG_FUNCTION(this << bridgeNetDevice);
    m_bridgesVisited.push_back(bridgeNetDevice);
}

} // namespace ns3
