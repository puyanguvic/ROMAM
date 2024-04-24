/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef DGR_HELPER_H
#define DGR_HELPER_H

#include "ns3/ipv4-routing-helper.h"
#include "ns3/net-device-container.h"
#include "ns3/node-container.h"
#include "ns3/object-factory.h"
#include "ns3/queue-disc-container.h"
#include "ns3/queue.h"
#include "ns3/romam-module.h"

namespace ns3
{

/**
 * \ingroup ipv4Helpers
 *
 * \brief Helper class that adds ns3::Ipv4GlobalRouting objects
 */
class DGRHelper : public Ipv4RoutingHelper
{
  public:
    /**
     * \brief Construct a GlobalRoutingHelper to make life easier for managing
     * global routing tasks.
     */
    DGRHelper();

    /**
     * \brief Construct a GlobalRoutingHelper from another previously initialized
     * instance (Copy Constructor).
     */
    DGRHelper(const DGRHelper&);

    /**
     * \returns pointer to clone of this Ipv4GlobalRoutingHelper
     *
     * This method is mainly for internal use by the other helpers;
     * clients are expected to free the dynamic memory allocated by this method
     */
    DGRHelper* Copy(void) const;

    /**
     * \param node the node on which the routing protocol will run
     * \returns a newly-created routing protocol
     *
     * This method will be called by ns3::InternetStackHelper::Install
     */
    virtual Ptr<Ipv4RoutingProtocol> Create(Ptr<Node> node) const;

    /**
     * \brief Build a routing database and initialize the routing tables of
     * the nodes in the simulation.  Makes all nodes in the simulation into
     * routers.
     *
     * All this function does is call the functions
     * BuildGlobalRoutingDatabase () and  InitializeRoutes ().
     *
     */
    static void PopulateRoutingTables(void);
    /**
     * \brief Remove all routes that were previously installed in a prior call
     * to either PopulateRoutingTables() or RecomputeRoutingTables(), and
     * add a new set of routes.
     *
     * This method does not change the set of nodes
     * over which GlobalRouting is being used, but it will dynamically update
     * its representation of the global topology before recomputing routes.
     * Users must first call PopulateRoutingTables() and then may subsequently
     * call RecomputeRoutingTables() at any later time in the simulation.
     *
     */
    static void RecomputeRoutingTables(void);

    /**
     * \param node Node
     * \return a QueueDisc container with the queue discs installed on the node
     *
     * \brief This method create the DGRv2 QueueDisc (along with it's internal queues,
     * classes) configured with the methods provided by this class and installs them
     * on the given node. Additionally, if configured, a queue limits object is
     * installed on each transmission queue of the device.
     */
    QueueDiscContainer Install(Ptr<Node> node);

    /**
     * \param c netdevic container
     * \return a QeueuDisc container with the root queue disc installed on the device
     *
     * \brief This method create the DGRv2 QueueDisc (along with it's internal queues,
     * classes) configured with the methods provided by this class and installs them
     * on the given node. Additionally, if configured, a queue limits object is
     * installed on each transmission queue of the device.
     */
    QueueDiscContainer Install(NetDeviceContainer c);

    /**
     * This method intall DGRv2Queue into NetDevice
     */
    QueueDiscContainer Install(Ptr<NetDevice> d);

  private:
    /**
     * \brief Assignment operator declared private and not implemented to disallow
     * assignment and prevent the compiler from happily inserting its own.
     * \return
     */
    DGRHelper& operator=(const DGRHelper&);
};

} // namespace ns3

#endif /* DGR_HELPER_H */