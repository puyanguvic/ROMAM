/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
#ifndef LSDB_H
#define LSDB_H

#include <stdint.h>
#include <list>
#include <queue>
#include <map>
#include <vector>

#include "ns3/ipv4-address.h"
#include "lsa.h"
#include "database.h"

namespace ns3
{

const uint32_t SPF_INFINITY = 0xffffffff; //!< "infinite" distance between nodes

/**
 * \ingroup globalrouting
 *
 * @brief Vertex used in shortest path first (SPF) computations. See \RFC{2328},
 * Section 16.
 *
 * Each router in the simulation is associated with an SPFVertex object.  When
 * calculating routes, each of these routers is, in turn, chosen as the "root"
 * of the calculation and routes to all of the other routers are eventually
 * saved in the routing tables of each of the chosen nodes.  Each of these
 * routers in the calculation has an associated SPFVertex.
 *
 * The "Root" vertex is the SPFVertex representing the router that is having
 * its routing tables set.  The SPFVertex objects representing other routers
 * or networks in the simulation are arranged in the SPF tree.  It is this
 * tree that represents the Shortest Paths to the other networks.
 *
 * Each SPFVertex has a pointer to the Global Router Link State Advertisement
 * (LSA) that its underlying router has exported.  Within these LSAs are
 * Global Router Link Records that describe the point to point links from the
 * underlying router to other nodes (represented by other SPFVertex objects)
 * in the simulation topology.  The combination of the arrangement of the
 * SPFVertex objects in the SPF tree, along with the details of the link
 * records that connect them provide the information required to construct the
 * required routes.
 */
class SPFVertex
{
  public:
    /**
     * @brief Enumeration of the possible types of SPFVertex objects.
     *
     * Currently we use VertexRouter to identify objects that represent a router
     * in the simulation topology, and VertexNetwork to identify objects that
     * represent a network.
     */
    enum VertexType
    {
        VertexUnknown = 0, /**< Uninitialized Link Record */
        VertexRouter,      /**< Vertex representing a router in the topology */
        VertexNetwork      /**< Vertex representing a network in the topology */
    };

    /**
     * @brief Construct an empty ("uninitialized") SPFVertex (Shortest Path First
     * Vertex).
     *
     * The Vertex Type is set to VertexUnknown, the Vertex ID is set to
     * 255.255.255.255, and the distance from root is set to infinity
     * (UINT32_MAX).  The referenced Link State Advertisement (LSA) is set to
     * null as is the parent SPFVertex.  The outgoing interface index is set to
     * infinity, the next hop address is set to 0.0.0.0 and the list of children
     * of the SPFVertex is initialized to empty.
     *
     * @see VertexType
     */
    SPFVertex();

    /**
     * @brief Construct an initialized SPFVertex (Shortest Path First Vertex).
     *
     * The Vertex Type is initialized to VertexRouter and the Vertex ID is found
     * from the Link State ID of the Link State Advertisement (LSA) passed as a
     * parameter.  The Link State ID is set to the Router ID of the advertising
     * router.  The referenced LSA (m_lsa) is set to the given LSA.  Other than
     * these members, initialization is as in the default constructor.
     * of the SPFVertex is initialized to empty.
     *
     * @see SPFVertex::SPFVertex ()
     * @see VertexType
     * @see LSA
     * @param lsa The Link State Advertisement used for finding initial values.
     */
    SPFVertex(LSA* lsa);

    /**
     * @brief Destroy an SPFVertex (Shortest Path First Vertex).
     *
     * The children vertices of the SPFVertex are recursively deleted.
     *
     * @see SPFVertex::SPFVertex ()
     */
    ~SPFVertex();

    // Delete copy constructor and assignment operator to avoid misuse
    SPFVertex(const SPFVertex&) = delete;
    SPFVertex& operator=(const SPFVertex&) = delete;

    /**
     * @brief Get the Vertex Type field of a SPFVertex object.
     *
     * The Vertex Type describes the kind of simulation object a given SPFVertex
     * represents.
     *
     * @see VertexType
     * @returns The VertexType of the current SPFVertex object.
     */
    VertexType GetVertexType() const;

    /**
     * @brief Set the Vertex Type field of a SPFVertex object.
     *
     * The Vertex Type describes the kind of simulation object a given SPFVertex
     * represents.
     *
     * @see VertexType
     * @param type The new VertexType for the current SPFVertex object.
     */
    void SetVertexType(VertexType type);

    /**
     * @brief Get the Vertex ID field of a SPFVertex object.
     *
     * The Vertex ID uniquely identifies the simulation object a given SPFVertex
     * represents.  Typically, this is the Router ID for SPFVertex objects
     * representing routers, and comes from the Link State Advertisement of a
     * router aggregated to a node in the simulation.  These IDs are allocated
     * automatically by the routing environment and look like IP addresses
     * beginning at 0.0.0.0 and monotonically increasing as new routers are
     * instantiated.
     *
     * @returns The Ipv4Address Vertex ID of the current SPFVertex object.
     */
    Ipv4Address GetVertexId() const;

    /**
     * @brief Set the Vertex ID field of a SPFVertex object.
     *
     * The Vertex ID uniquely identifies the simulation object a given SPFVertex
     * represents.  Typically, this is the Router ID for SPFVertex objects
     * representing routers, and comes from the Link State Advertisement of a
     * router aggregated to a node in the simulation.  These IDs are allocated
     * automatically by the routing environment and look like IP addresses
     * beginning at 0.0.0.0 and monotonically increase as new routers are
     * instantiated.  This method is an explicit override of the automatically
     * generated value.
     *
     * @param id The new Ipv4Address Vertex ID for the current SPFVertex object.
     */
    void SetVertexId(Ipv4Address id);

    /**
     * @brief Get the Global Router Link State Advertisement returned by the
     * Global Router represented by this SPFVertex during the route discovery
     * process.
     *
     * @see GlobalRouter
     * @see LSA
     * @see GlobalRouter::DiscoverLSAs ()
     * @returns A pointer to the LSA found by the router represented
     * by this SPFVertex object.
     */
    LSA* GetLSA() const;

    /**
     * @brief Set the Global Router Link State Advertisement returned by the
     * Global Router represented by this SPFVertex during the route discovery
     * process.
     *
     * @see SPFVertex::GetLSA ()
     * @see GlobalRouter
     * @see LSA
     * @see GlobalRouter::DiscoverLSAs ()
     * @warning Ownership of the LSA is transferred to the "this" SPFVertex.  You
     * must not delete the LSA after calling this method.
     * @param lsa A pointer to the LSA.
     */
    void SetLSA(LSA* lsa);

    /**
     * @brief Get the distance from the root vertex to "this" SPFVertex object.
     *
     * Each router in the simulation is associated with an SPFVertex object.  When
     * calculating routes, each of these routers is, in turn, chosen as the "root"
     * of the calculation and routes to all of the other routers are eventually
     * saved in the routing tables of each of the chosen nodes.  Each of these
     * routers in the calculation has an associated SPFVertex.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set.  The "this" SPFVertex is the vertex to which
     * a route is being calculated from the root.  The distance from the root that
     * we're asking for is the number of hops from the root vertex to the vertex
     * in question.
     *
     * The distance is calculated during route discovery and is stored in a
     * member variable.  This method simply fetches that value.
     *
     * @returns The distance, in hops, from the root SPFVertex to "this" SPFVertex.
     */
    uint32_t GetDistanceFromRoot() const;

    /**
     * @brief Set the distance from the root vertex to "this" SPFVertex object.
     *
     * Each router in the simulation is associated with an SPFVertex object.  When
     * calculating routes, each of these routers is, in turn, chosen as the "root"
     * of the calculation and routes to all of the other routers are eventually
     * saved in the routing tables of each of the chosen nodes.  Each of these
     * routers in the calculation has an associated SPFVertex.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set.  The "this" SPFVertex is the vertex to which
     * a route is being calculated from the root.  The distance from the root that
     * we're asking for is the number of hops from the root vertex to the vertex
     * in question.
     *
     * @param distance The distance, in hops, from the root SPFVertex to "this"
     * SPFVertex.
     */
    void SetDistanceFromRoot(uint32_t distance);

    /**
     * @brief Set the IP address and outgoing interface index that should be used
     * to begin forwarding packets from the root SPFVertex to "this" SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set.  The "this" SPFVertex is the vertex that
     * represents the host or network to which a route is being calculated from
     * the root.  The IP address that we're asking for is the address on the
     * remote side of a link off of the root node that should be used as the
     * destination for packets along the path to "this" vertex.
     *
     * When initializing the root SPFVertex, the IP address used when forwarding
     * packets is determined by examining the Global Router Link Records of the
     * Link State Advertisement generated by the root node's GlobalRouter.  This
     * address is used to forward packets off of the root's network down those
     * links.  As other vertices / nodes are discovered which are further away
     * from the root, they will be accessible down one of the paths via a link
     * described by one of these Global Router Link Records.
     *
     * To forward packets to these hosts or networks, the root node must begin
     * the forwarding process by sending the packets to a first hop router down
     * an interface.  This means that the first hop address and interface ID must
     * be the same for all downstream SPFVertices.  We call this "inheriting"
     * the interface and next hop.
     *
     * In this method we are telling the root node which exit direction it should send
     * should I send a packet to the network or host represented by 'this' SPFVertex.
     *
     * @see GlobalRouter
     * @see LSA
     * @see GlobalRoutingLinkRecord
     * @param nextHop The IP address to use when forwarding packets to the host
     * or network represented by "this" SPFVertex.
     * @param id The interface index to use when forwarding packets to the host or
     * network represented by "this" SPFVertex.
     */
    void SetRootExitDirection(Ipv4Address nextHop, int32_t id = SPF_INFINITY);

    typedef std::pair<Ipv4Address, int32_t>
        NodeExit_t; //!< IPv4 / interface container for exit nodes.

    /**
     * @brief Set the IP address and outgoing interface index that should be used
     * to begin forwarding packets from the root SPFVertex to "this" SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set.  The "this" SPFVertex is the vertex that
     * represents the host or network to which a route is being calculated from
     * the root.  The IP address that we're asking for is the address on the
     * remote side of a link off of the root node that should be used as the
     * destination for packets along the path to "this" vertex.
     *
     * When initializing the root SPFVertex, the IP address used when forwarding
     * packets is determined by examining the Global Router Link Records of the
     * Link State Advertisement generated by the root node's GlobalRouter.  This
     * address is used to forward packets off of the root's network down those
     * links.  As other vertices / nodes are discovered which are further away
     * from the root, they will be accessible down one of the paths via a link
     * described by one of these Global Router Link Records.
     *
     * To forward packets to these hosts or networks, the root node must begin
     * the forwarding process by sending the packets to a first hop router down
     * an interface.  This means that the first hop address and interface ID must
     * be the same for all downstream SPFVertices.  We call this "inheriting"
     * the interface and next hop.
     *
     * In this method we are telling the root node which exit direction it should send
     * should I send a packet to the network or host represented by 'this' SPFVertex.
     *
     * @see GlobalRouter
     * @see LSA
     * @see GlobalRoutingLinkRecord
     * @param exit The pair of next-hop-IP and outgoing-interface-index to use when
     * forwarding packets to the host or network represented by "this" SPFVertex.
     */
    void SetRootExitDirection(SPFVertex::NodeExit_t exit);
    /**
     * \brief Obtain a pair indicating the exit direction from the root
     *
     * \param i An index to a pair
     * \return A pair of next-hop-IP and outgoing-interface-index for
     * indicating an exit direction from the root. It is 0 if the index 'i'
     * is out-of-range
     */
    NodeExit_t GetRootExitDirection(uint32_t i) const;
    /**
     * \brief Obtain a pair indicating the exit direction from the root
     *
     * This method assumes there is only a single exit direction from the root.
     * Error occur if this assumption is invalid.
     *
     * \return The pair of next-hop-IP and outgoing-interface-index for reaching
     * 'this' vertex from the root
     */
    NodeExit_t GetRootExitDirection() const;
    /**
     * \brief Merge into 'this' vertex the list of exit directions from
     * another vertex
     *
     * This merge is necessary when ECMP are found.
     *
     * \param vertex From which the list of exit directions are obtain
     * and are merged into 'this' vertex
     */
    void MergeRootExitDirections(const SPFVertex* vertex);
    /**
     * \brief Inherit all root exit directions from a given vertex to 'this' vertex
     * \param vertex The vertex from which all root exit directions are to be inherited
     *
     * After the call of this method, the original root exit directions
     * in 'this' vertex are all lost.
     */
    void InheritAllRootExitDirections(const SPFVertex* vertex);
    /**
     * \brief Get the number of exit directions from root for reaching 'this' vertex
     * \return The number of exit directions from root
     */
    uint32_t GetNRootExitDirections() const;

    /**
     * @brief Get a pointer to the SPFVector that is the parent of "this"
     * SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set and is the root of the SPF tree.
     *
     * This method returns a pointer to the parent node of "this" SPFVertex
     * (both of which reside in that SPF tree).
     *
     * @param i The index to one of the parents
     * @returns A pointer to the SPFVertex that is the parent of "this" SPFVertex
     * in the SPF tree.
     */
    SPFVertex* GetParent(uint32_t i = 0) const;

    /**
     * @brief Set the pointer to the SPFVector that is the parent of "this"
     * SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set and is the root of the SPF tree.
     *
     * This method sets the parent pointer of "this" SPFVertex (both of which
     * reside in that SPF tree).
     *
     * @param parent A pointer to the SPFVertex that is the parent of "this"
     * SPFVertex* in the SPF tree.
     */
    void SetParent(SPFVertex* parent);
    /**
     * \brief Merge the Parent list from the v into this vertex
     *
     * \param v The vertex from which its list of Parent is read
     * and then merged into the list of Parent of *this* vertex.
     * Note that the list in v remains intact
     */
    void MergeParent(const SPFVertex* v);

    /**
     * @brief Get the number of children of "this" SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set and is the root of the SPF tree.  Each vertex
     * in the SPF tree can have a number of children that represent host or
     * network routes available via that vertex.
     *
     * This method returns the number of children of "this" SPFVertex (which
     * reside in the SPF tree).
     *
     * @returns The number of children of "this" SPFVertex (which reside in the
     * SPF tree).
     */
    uint32_t GetNChildren() const;

    /**
     * @brief Get a borrowed SPFVertex pointer to the specified child of "this"
     * SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set and is the root of the SPF tree.  Each vertex
     * in the SPF tree can have a number of children that represent host or
     * network routes available via that vertex.
     *
     * This method the number of children of "this" SPFVertex (which reside in
     * the SPF tree.
     *
     * @see SPFVertex::GetNChildren
     * @param n The index (from 0 to the number of children minus 1) of the
     * child SPFVertex to return.
     * @warning The pointer returned by GetChild () is a borrowed pointer.  You
     * do not have any ownership of the underlying object and must not delete
     * that object.
     * @returns A pointer to the specified child SPFVertex (which resides in the
     * SPF tree).
     */
    SPFVertex* GetChild(uint32_t n) const;

    /**
     * @brief Get a borrowed SPFVertex pointer to the specified child of "this"
     * SPFVertex.
     *
     * Each router node in the simulation is associated with an SPFVertex object.
     * When calculating routes, each of these routers is, in turn, chosen as the
     * "root" of the calculation and routes to all of the other routers are
     * eventually saved in the routing tables of each of the chosen nodes.
     *
     * The "Root" vertex is then the SPFVertex representing the router that is
     * having its routing tables set and is the root of the SPF tree.  Each vertex
     * in the SPF tree can have a number of children that represent host or
     * network routes available via that vertex.
     *
     * This method the number of children of "this" SPFVertex (which reside in
     * the SPF tree.
     *
     * @see SPFVertex::GetNChildren
     * @warning Ownership of the pointer added to the children of "this"
     * SPFVertex is transferred to the "this" SPFVertex.  You must not delete the
     * (now) child SPFVertex after calling this method.
     * @param child A pointer to the SPFVertex (which resides in the SPF tree) to
     * be added to the list of children of "this" SPFVertex.
     * @returns The number of children of "this" SPFVertex after the addition of
     * the new child.
     */
    uint32_t AddChild(SPFVertex* child);

    /**
     * @brief Set the value of the VertexProcessed flag
     *
     * Flag to note whether vertex has been processed in stage two of
     * SPF computation
     * @param value boolean value to set the flag
     */
    void SetVertexProcessed(bool value);

    /**
     * @brief Check the value of the VertexProcessed flag
     *
     * Flag to note whether vertex has been processed in stage two of
     * SPF computation
     * @returns value of underlying flag
     */
    bool IsVertexProcessed() const;

    /**
     * @brief Clear the value of the VertexProcessed flag
     *
     * Flag to note whether vertex has been processed in stage two of
     * SPF computation
     */
    void ClearVertexProcessed();

  private:
    VertexType m_vertexType;                        //!< Vertex type
    Ipv4Address m_vertexId;                         //!< Vertex ID
    LSA* m_lsa;                        //!< Link State Advertisement
    uint32_t m_distanceFromRoot;                    //!< Distance from root node
    int32_t m_rootOif;                              //!< root Output Interface
    Ipv4Address m_nextHop;                          //!< next hop
    typedef std::list<NodeExit_t> ListOfNodeExit_t; //!< container of Exit nodes
    ListOfNodeExit_t m_ecmpRootExits; //!< store the multiple root's exits for supporting ECMP
    typedef std::list<SPFVertex*> ListOfSPFVertex_t; //!< container of SPFVertexes
    ListOfSPFVertex_t m_parents;                     //!< parent list
    ListOfSPFVertex_t m_children;                    //!< Children list
    bool m_vertexProcessed; //!< Flag to note whether vertex has been processed in stage two of SPF
                            //!< computation

    /**
     * \brief Stream insertion operator.
     *
     * \param os the reference to the output stream
     * \param vs a list of SPFVertexes
     * \returns the reference to the output stream
     */
    friend std::ostream& operator<<(std::ostream& os, const SPFVertex::ListOfSPFVertex_t& vs);
};

/**
 * @brief The Link State DataBase (LSDB) of the Global Route Manager.
 *
 * Each node in the simulation participating in global routing has a
 * GlobalRouter interface.  The primary job of this interface is to export
 * Global Router Link State Advertisements (LSAs).  These advertisements in
 * turn contain a number of Global Router Link Records that describe the
 * point to point links from the underlying node to other nodes (that will
 * also export their own LSAs.
 *
 * This class implements a searchable database of LSAs gathered from every
 * router in the simulation.
 */
class LSDB : public Database
{
  public:
    /**
      * \brief Get the type ID.
      * \return the object TypeId
      */
    static TypeId GetTypeId();
    
    /**
     * @brief Construct an empty Global Router Manager Link State Database.
     *
     * The database map composing the Link State Database is initialized in
     * this constructor.
     */
    LSDB();

    /**
     * @brief Destroy an empty Global Router Manager Link State Database.
     *
     * The database map is walked and all of the Link State Advertisements stored
     * in the database are freed; then the database map itself is clear ()ed to
     * release any remaining resources.
     */
    ~LSDB();

    // Delete copy constructor and assignment operator to avoid misuse
    LSDB(const LSDB&) = delete;
    LSDB& operator=(const LSDB&) = delete;

    /**
     * @brief Insert an IP address / Link State Advertisement pair into the Link
     * State Database.
     *
     * The IPV4 address and the LSA given as parameters are converted
     * to an STL pair and are inserted into the database map.
     *
     * @see LSA
     * @see Ipv4Address
     * @param addr The IP address associated with the LSA.  Typically the Router
     * ID.
     * @param lsa A pointer to the Link State Advertisement for the router.
     */
    void Insert(Ipv4Address addr, LSA* lsa);

    /**
     * @brief Look up the Link State Advertisement associated with the given
     * link state ID (address).
     *
     * The database map is searched for the given IPV4 address and corresponding
     * LSA is returned.
     *
     * @see LSA
     * @see Ipv4Address
     * @param addr The IP address associated with the LSA.  Typically the Router
     * ID.
     * @returns A pointer to the Link State Advertisement for the router specified
     * by the IP address addr.
     */
    LSA* GetLSA(Ipv4Address addr) const;
    /**
     * @brief Look up the Link State Advertisement associated with the given
     * link state ID (address).  This is a variation of the GetLSA call
     * to allow the LSA to be found by matching addr with the LinkData field
     * of the TransitNetwork link record.
     *
     * @see GetLSA
     * @param addr The IP address associated with the LSA.  Typically the Router
     * @returns A pointer to the Link State Advertisement for the router specified
     * by the IP address addr.
     * ID.
     */
    LSA* GetLSAByLinkData(Ipv4Address addr) const;

    /**
     * @brief Set all LSA flags to an initialized state, for SPF computation
     *
     * This function walks the database and resets the status flags of all of the
     * contained Link State Advertisements to LSA_SPF_NOT_EXPLORED.  This is done
     * prior to each SPF calculation to reset the state of the SPFVertex structures
     * that will reference the LSAs during the calculation.
     *
     * @see LSA
     * @see SPFVertex
     */
    void Initialize();

    /**
     * @brief Look up the External Link State Advertisement associated with the given
     * index.
     *
     * The external database map is searched for the given index and corresponding
     * LSA is returned.
     *
     * @see LSA
     * @param index the index associated with the LSA.
     * @returns A pointer to the Link State Advertisement.
     */
    LSA* GetExtLSA(uint32_t index) const;
    /**
     * @brief Get the number of External Link State Advertisements.
     *
     * @see LSA
     * @returns the number of External Link State Advertisements.
     */
    uint32_t GetNumExtLSAs() const;

    void Print (std::ostream &os) const;

  private:
    typedef std::map<Ipv4Address, LSA*>
        LSDBMap_t; //!< container of IPv4 addresses / Link State Advertisements
    typedef std::pair<Ipv4Address, LSA*>
        LSDBPair_t; //!< pair of IPv4 addresses / Link State Advertisements

    LSDBMap_t m_database; //!< database of IPv4 addresses / Link State Advertisements
    std::vector<LSA*>
        m_extdatabase; //!< database of External Link State Advertisements
};

} // namespace ns3

#endif /* LSDB_H*/
