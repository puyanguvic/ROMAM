/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#ifndef GLOBAL_LSDB_MANAGER_H
#define GLOBAL_LSDB_MANAGER_H

#include "lsdb.h"

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

class GlobalLSDBManager
{
  public:
    GlobalLSDBManager();
    virtual ~GlobalLSDBManager();

    /**
     * @brief Build the Link State Database (LSDB) by gathering Link State Advertisements
     * from each node exporting a Router interface.
     */
    virtual void BuildLinkStateDatabase();

    /**
     * @brief Delete the Link State Database (LSDB), create a new one.
     */
    void DeleteLinkStateDatabase();

    /**
     * @brief Get LSDB
     * @return LSDB
     */
    LSDB* GetLSDB(void) const;

  private:
    Vertex* m_spfroot; //!< the root node
    LSDB* m_lsdb;      //!< the Link State DataBase (LSDB) of the Global Route Manager
};

} // namespace ns3

#endif /* GLOBAL_LSDB_MANAGER_H */