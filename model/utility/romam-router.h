/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef ROMAM_ROUTER_H
#define ROMAM_ROUTER_H

#include "ns3/bridge-net-device.h"
#include "ns3/channel.h"
#include "ns3/ipv4-address.h"
#include "ns3/net-device-container.h"
#include "ns3/node.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <list>
#include <stdint.h>

namespace ns3
{

class LSA;
class DijkstraRIE;
class RomamRouting;

class RomamRouter : public Object
{
  public:
    /**
     * \brief Get the type ID.
     * \return the object TypeId
     */
    static TypeId GetTypeId();

    /**
     * @brief Create a Global Router class
     */
    RomamRouter();

    // Delete copy constructor and assignment operator to avoid misuse
    RomamRouter(const RomamRouter&) = delete;
    RomamRouter& operator=(const RomamRouter&) = delete;

    /**
     * @brief Get the Router ID associated with this Global Router.
     *
     * The Router IDs are allocated in the RoutingEnvironment -- one per Router,
     * starting at 0.0.0.1 and incrementing with each instantiation of a router.
     *
     * @see RoutingEnvironment::AllocateRouterId ()
     * @returns The Router ID associated with the Global Router.
     */
    Ipv4Address GetRouterId() const;

    /**
     * @brief Walk the connected channels, discover the adjacent routers and build
     * the associated number of Global Routing Link State Advertisements that
     * this router can export.
     *
     * This is a fairly expensive operation in that every time it is called
     * the current list of LSAs is built by walking connected point-to-point
     * channels and peeking into adjacent IPV4 stacks to get address information.
     * This is done to allow for limited dynamics of the Global Routing
     * environment.  By that we mean that you can discover new link state
     * advertisements after a network topology change by calling DiscoverLSAs
     * and then by reading those advertisements.
     *
     * @see GlobalRoutingLSA
     * @see RomamRouter::GetLSA ()
     * @returns The number of Global Routing Link State Advertisements.
     */
    uint32_t DiscoverLSAs();

    /**
     * @brief Get the Number of Global Routing Link State Advertisements that this
     * router can export.
     *
     * To get meaningful information you must have previously called DiscoverLSAs.
     * After you know how many LSAs are present in the router, you may call
     * GetLSA () to retrieve the actual advertisement.
     *
     * @see RomamRouterLSA
     * @see GlobalRouting::DiscoverLSAs ()
     * @see GlobalRouting::GetLSA ()
     * @returns The number of Global Routing Link State Advertisements.
     */
    uint32_t GetNumLSAs() const;

    /**
     * @brief Get a Global Routing Link State Advertisements that this router has
     * said that it can export.
     *
     * This is a fairly inexpensive expensive operation in that the hard work
     * was done in GetNumLSAs.  We just copy the indicated Global Routing Link
     * State Advertisement into the requested GlobalRoutingLSA object.
     *
     * You must call RomamRouter::GetNumLSAs before calling this method in
     * order to discover the adjacent routers and build the advertisements.
     * GetNumLSAs will return the number of LSAs this router advertises.
     * The parameter n (requested LSA number) must be in the range 0 to
     * GetNumLSAs() - 1.
     *
     * @see GlobalRoutingLSA
     * @see GlobalRouting::GetNumLSAs ()
     * @param n The index number of the LSA you want to read.
     * @param lsa The GlobalRoutingLSA class to receive the LSA information.
     * @returns The number of Global Router Link State Advertisements.
     */
    bool GetLSA(uint32_t n, LSA& lsa) const;

    /**
     * @brief Inject a route to be circulated to other routers as an external
     * route
     *
     * @param network The Network to inject
     * @param networkMask The Network Mask to inject
     */
    void InjectRoute(Ipv4Address network, Ipv4Mask networkMask);

    /**
     * @brief Get the number of injected routes that have been added
     * to the routing table.
     * @return number of injected routes
     */
    uint32_t GetNInjectedRoutes();

    /**
     * @brief Return the injected route indexed by i
     * @param i the index of the route
     * @return a pointer to that Ipv4RoutingTableEntry is returned
     *
     */
    DijkstraRIE* GetInjectedRoute(uint32_t i);

    /**
     * @brief Withdraw a route from the global unicast routing table.
     *
     * Calling this function will cause all indexed routes numbered above
     * index i to have their index decremented.  For instance, it is possible to
     * remove N injected routes by calling RemoveInjectedRoute (0) N times.
     *
     * @param i The index (into the injected routing list) of the route to remove.
     *
     * @see RomamRouter::WithdrawRoute ()
     */
    void RemoveInjectedRoute(uint32_t i);

    /**
     * @brief Withdraw a route from the global unicast routing table.
     *
     * @param network The Network to withdraw
     * @param networkMask The Network Mask to withdraw
     * @return whether the operation succeeded (will return false if no such route)
     *
     * @see RomamRouter::RemoveInjectedRoute ()
     */
    bool WithdrawRoute(Ipv4Address network, Ipv4Mask networkMask);

    /**
     * \brief Set the specific Global Routing Protocol to be used
     * \param routing the routing protocol
     */
    virtual void SetRoutingProtocol(Ptr<RomamRouting> routing) = 0;

    /**
     * \brief Get the specific Global Routing Protocol used
     * \returns the routing protocol
     */
    virtual Ptr<RomamRouting> GetRoutingProtocol() = 0;

  protected:
    virtual ~RomamRouter() override;
    // inherited from Object
    virtual void DoDispose() override;

  private:
    /**
     * \brief Clear list of LSAs
     */
    void ClearLSAs();

    /**
     * \brief Link through the given channel and find the net device that's on the other end.
     *
     * This only makes sense with a point-to-point channel.
     *
     * \param nd outgoing NetDevice
     * \param ch channel
     * \returns the NetDevice on the other end
     */
    Ptr<NetDevice> GetAdjacent(Ptr<NetDevice> nd, Ptr<Channel> ch) const;

    /**
     * \brief Finds a designated router
     *
     * Given a local net device, we need to walk the channel to which the net device is
     * attached and look for nodes with RomamRouter interfaces on them (one of them
     * will be us).  Of these, the router with the lowest IP address on the net device
     * connecting to the channel becomes the designated router for the link.
     *
     * \param ndLocal local NetDevice to scan
     * \returns the IP address of the designated router
     */
    Ipv4Address FindDesignatedRouterForLink(Ptr<NetDevice> ndLocal) const;

    /**
     * \brief Checks for the presence of another router on the NetDevice
     *
     * Given a node and an attached net device, take a look off in the channel to
     * which the net device is attached and look for a node on the other side
     * that has a RomamRouter interface aggregated.
     *
     * \param nd NetDevice to scan
     * \returns true if a router is found
     */
    bool AnotherRouterOnLink(Ptr<NetDevice> nd) const;

    /**
     * \brief Process a generic broadcast link
     *
     * \param nd the NetDevice
     * \param pLSA the Global LSA
     * \param c the returned NetDevice container
     */
    void ProcessBroadcastLink(Ptr<NetDevice> nd, LSA* pLSA, NetDeviceContainer& c);

    /**
     * \brief Process a single broadcast link
     *
     * \param nd the NetDevice
     * \param pLSA the Global LSA
     * \param c the returned NetDevice container
     */
    void ProcessSingleBroadcastLink(Ptr<NetDevice> nd, LSA* pLSA, NetDeviceContainer& c);

    /**
     * \brief Process a bridged broadcast link
     *
     * \param nd the NetDevice
     * \param pLSA the Global LSA
     * \param c the returned NetDevice container
     */
    void ProcessBridgedBroadcastLink(Ptr<NetDevice> nd, LSA* pLSA, NetDeviceContainer& c);

    /**
     * \brief Process a point to point link
     *
     * \param ndLocal the NetDevice
     * \param pLSA the Global LSA
     */
    void ProcessPointToPointLink(Ptr<NetDevice> ndLocal, LSA* pLSA);

    /**
     * \brief Build one NetworkLSA for each net device talking to a network that we are the
     * designated router for.
     *
     * \param c the devices.
     */
    void BuildNetworkLSAs(NetDeviceContainer c);

    /**
     * \brief Return a container of all non-bridged NetDevices on a link
     *
     * This method will recursively find all of the 'edge' devices in an
     * L2 broadcast domain.  If there are no bridged devices, then the
     * container returned is simply the set of devices on the channel
     * passed in as an argument.  If the link has bridges on it
     * (and therefore multiple ns3::Channel objects interconnected by
     * bridges), the method will find all of the non-bridged devices
     * in the L2 broadcast domain.
     *
     * \param ch a channel from the link
     * \returns the NetDeviceContainer.
     */
    NetDeviceContainer FindAllNonBridgedDevicesOnLink(Ptr<Channel> ch) const;

    /**
     * \brief Decide whether or not a given net device is being bridged by a BridgeNetDevice.
     *
     * \param nd the NetDevice
     * \returns the BridgeNetDevice smart pointer or null if not found
     */
    Ptr<BridgeNetDevice> NetDeviceIsBridged(Ptr<NetDevice> nd) const;

    typedef std::list<LSA*> ListOfLSAs_t; //!< container for the GlobalRoutingLSAs
    ListOfLSAs_t m_LSAs;                  //!< database of GlobalRoutingLSAs

    Ipv4Address m_routerId; //!< router ID (its IPv4 address)
    // Ptr<Ipv4GlobalRouting> m_routingProtocol; //!< the Ipv4GlobalRouting in use

    typedef std::list<DijkstraRIE*> InjectedRoutes; //!< container of Ipv4RoutingTableEntry
    typedef std::list<DijkstraRIE*>::const_iterator
        InjectedRoutesCI; //!< Const Iterator to container of Ipv4RoutingTableEntry
    typedef std::list<DijkstraRIE*>::iterator
        InjectedRoutesI;             //!< Iterator to container of Ipv4RoutingTableEntry
    InjectedRoutes m_injectedRoutes; //!< Routes we are exporting

    // Declared mutable so that const member functions can clear it
    // (supporting the logical constness of the search methods of this class)
    /**
     * Container of bridges visited.
     */
    mutable std::vector<Ptr<BridgeNetDevice>> m_bridgesVisited;
    /**
     * Clear the list of bridges visited on the link
     */
    void ClearBridgesVisited() const;
    /**
     * When recursively checking for devices on the link, check whether a
     * given device has already been visited.
     *
     * \param device the bridge device to check
     * \return true if bridge has already been visited
     */
    bool BridgeHasAlreadyBeenVisited(Ptr<BridgeNetDevice> device) const;
    /**
     * When recursively checking for devices on the link, mark a given device
     * as having been visited.
     *
     * \param device the bridge device to mark
     */
    void MarkBridgeAsVisited(Ptr<BridgeNetDevice> device) const;
};

} // namespace ns3

#endif /* ROMAM_ROUTER_H */
