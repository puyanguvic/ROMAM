/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef DIJKSTRA_ALGORITHM_H
#define DIJKSTRA_ALGORITHM_H

#include "route-candidate-queue.h"
#include "routing-algorithm.h"

#include "ns3/ipv4-address.h"
#include "ns3/object.h"
#include "ns3/ptr.h"

#include <list>
#include <map>
#include <queue>
#include <stdint.h>
#include <vector>

namespace ns3
{

// class RouteCandidateQueue;
// class Ipv4DGRRouting;
class LSDB;
class RomamRouter;

class DijkstraAlgorithm : public RoutingAlgorithm
{
  public:
    DijkstraAlgorithm();
    virtual ~DijkstraAlgorithm();

    // Delete copy constructor and assignment operator to avoid misuse
    DijkstraAlgorithm(const DijkstraAlgorithm&) = delete;
    DijkstraAlgorithm& operator=(const DijkstraAlgorithm&) = delete;

    /**
     * @brief Delete all static routes on all nodes that have a
     * Romam Router Interface
     *
     * \todo  separate manually assigned static routes from static routes that
     * the global routing code injects, and only delete the latter
     */
    void DeleteRoutes() override;

    /**
     * @brief Compute routes using a dijkstra SPF computation and
     * populate per-node forwarding tables
     */
    void InitializeRoutes() override;

    void InsertLSDB(LSDB* lsdb);

  private:
    Vertex* m_spfroot; //!< the root node
    LSDB* m_lsdb;      //!< the Link State DataBase (LSDB)

    /**
     * \brief Test if a node is a stub, from an OSPF sense.
     *
     * If there is only one link of type 1 or 2, then a default route
     * can safely be added to the next-hop router and SPF does not need
     * to be run
     *
     * \param root the root node
     * \returns true if the node is a stub
     */
    bool CheckForStubNode(Ipv4Address root);

    /**
     * \brief Calculate the shortest path first (SPF) tree
     *
     * Equivalent to quagga ospf_spf_calculate
     * \param root the root node
     */
    void SPFCalculate(Ipv4Address root);

    /**
     * \brief Process Stub nodes
     *
     * Processing logic from RFC 2328, page 166 and quagga ospf_spf_process_stubs ()
     * stub link records will exist for point-to-point interfaces and for
     * broadcast interfaces for which no neighboring router can be found
     *
     * \param v vertex to be processed
     */
    void SPFProcessStubs(Vertex* v);

    /**
     * \brief Process Autonomous Systems (AS) External LSA
     *
     * \param v vertex to be processed
     * \param extlsa external LSA
     */
    void ProcessASExternals(Vertex* v, LSA* extlsa);

    /**
     * \brief Examine the links in v's LSA and update the list of candidates with any
     *        vertices not already on the list
     *
     * \internal
     *
     * This method is derived from quagga ospf_spf_next ().  See RFC2328 Section
     * 16.1 (2) for further details.
     *
     * We're passed a parameter \a v that is a vertex which is already in the SPF
     * tree.  A vertex represents a router node.  We also get a reference to the
     * SPF candidate queue, which is a priority queue containing the shortest paths
     * to the networks we know about.
     *
     * We examine the links in v's LSA and update the list of candidates with any
     * vertices not already on the list.  If a lower-cost path is found to a
     * vertex already on the candidate list, store the new (lower) cost.
     *
     * \param v the vertex
     * \param candidate the SPF candidate queue
     */
    void SPFNext(Vertex* v, RouteCandidateQueue& candidate);

    /**
     * \brief Calculate nexthop from root through V (parent) to vertex W (destination)
     *        with given distance from root->W.
     *
     * This method is derived from quagga ospf_nexthop_calculation() 16.1.1.
     * For now, this is greatly simplified from the quagga code
     *
     * \param v the parent
     * \param w the destination
     * \param l the link record
     * \param distance the target distance
     * \returns 1 on success
     */
    int SPFNexthopCalculation(Vertex* v, Vertex* w, LinkRecord* l, uint32_t distance);

    /**
     * \brief Adds a vertex to the list of children *in* each of its parents
     *
     * Derived from quagga ospf_vertex_add_parents ()
     *
     * This is a somewhat oddly named method (blame quagga).  Although you might
     * expect it to add a parent *to* something, it actually adds a vertex
     * to the list of children *in* each of its parents.
     *
     * Given a pointer to a vertex, it links back to the vertex's parent that it
     * already has set and adds itself to that vertex's list of children.
     *
     * \param v the vertex
     */
    void SPFVertexAddParent(Vertex* v);

    /**
     * \brief Search for a link between two vertices.
     *
     * This method is derived from quagga ospf_get_next_link ()
     *
     * First search the Global Router Link Records of vertex \a v for one
     * representing a point-to point link to vertex \a w.
     *
     * What is done depends on prev_link.  Contrary to appearances, prev_link just
     * acts as a flag here.  If prev_link is NULL, we return the first Global
     * Router Link Record we find that describes a point-to-point link from \a v
     * to \a w.  If prev_link is not NULL, we return a Global Router Link Record
     * representing a possible *second* link from \a v to \a w.
     *
     * \param v first vertex
     * \param w second vertex
     * \param prev_link the previous link in the list
     * \returns the link's record
     */
    LinkRecord* SPFGetNextLink(Vertex* v, Vertex* w, LinkRecord* prev_link);

    /**
     * \brief Add a host route to the routing tables
     *
     *
     * This method is derived from quagga ospf_intra_add_router ()
     *
     * This is where we are actually going to add the host routes to the routing
     * tables of the individual nodes.
     *
     * The vertex passed as a parameter has just been added to the SPF tree.
     * This vertex must have a valid m_root_oid, corresponding to the outgoing
     * interface on the root router of the tree that is the first hop on the path
     * to the vertex.  The vertex must also have a next hop address, corresponding
     * to the next hop on the path to the vertex.  The vertex has an m_lsa field
     * that has some number of link records.  For each point to point link record,
     * the m_linkData is the local IP address of the link.  This corresponds to
     * a destination IP address, reachable from the root, to which we add a host
     * route.
     *
     * \param v the vertex
     *
     */
    void SPFIntraAddRouter(Vertex* v);

    /**
     * \brief Add a transit to the routing tables
     *
     * \param v the vertex
     */
    void SPFIntraAddTransit(Vertex* v);

    /**
     * \brief Add a stub to the routing tables
     *
     * \param l the global routing link record
     * \param v the vertex
     */
    void SPFIntraAddStub(LinkRecord* l, Vertex* v);

    /**
     * \brief Add an external route to the routing tables
     *
     * \param extlsa the external LSA
     * \param v the vertex
     */
    void SPFAddASExternal(LSA* extlsa, Vertex* v);

    /**
     * \brief Return the interface number corresponding to a given IP address and mask
     *
     * This is a wrapper around GetInterfaceForPrefix(), but we first
     * have to find the right node pointer to pass to that function.
     * If no such interface is found, return -1 (note:  unit test framework
     * for routing assumes -1 to be a legal return value)
     *
     * \param a the target IP address
     * \param amask the target subnet mask
     * \return the outgoing interface number
     */
    int32_t FindOutgoingInterfaceId(Ipv4Address a, Ipv4Mask amask = Ipv4Mask("255.255.255.255"));
};

} // namespace ns3
#endif /* DIJKSTRA_ALGORITHM_H */
