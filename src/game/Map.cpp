/*
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * Copyright (C) 2008-2009 Trinity <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "MapManager.h"
#include "Player.h"
#include "TemporarySummon.h"
#include "GridNotifiers.h"
#include "WorldSession.h"
#include "Log.h"
#include "GridStates.h"
#include "CellImpl.h"
#include "InstanceData.h"
#include "Map.h"
#include "GridNotifiersImpl.h"
#include "Transports.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "World.h"
#include "ScriptMgr.h"
#include "Group.h"
#include "MapRefManager.h"
#include "WaypointManager.h"
#include "BattleGround.h"
#include "GridMap.h"

#include "MapInstanced.h"
#include "InstanceSaveMgr.h"
#include "VMapFactory.h"

#define DEFAULT_GRID_EXPIRY     300
#define MAX_GRID_LOAD_TIME      50
#define MAX_CREATURE_ATTACK_RADIUS  (45.0f * sWorld.getRate(RATE_CREATURE_AGGRO))

struct ScriptAction
{
    uint64 sourceGUID;
    uint64 targetGUID;
    uint64 ownerGUID;                                       // owner of source if source is item
    ScriptInfo const* script;                               // pointer to static script data
};

GridState* si_GridStates[MAX_GRID_STATE];

Map::~Map()
{
    UnloadAll();

    if (!m_scriptSchedule.empty())
        sWorld.DecreaseScheduledScriptCount(m_scriptSchedule.size());
}

void Map::LoadVMap(int x,int y)
{
                                                            // x and y are swapped !!
    int vmapLoadResult = VMAP::VMapFactory::createOrGetVMapManager()->loadMap((sWorld.GetDataPath()+ "vmaps").c_str(),  GetId(), x,y);
    switch (vmapLoadResult)
    {
        case VMAP::VMAP_LOAD_RESULT_OK:
            sLog.outDetail("VMAP loaded name:%s, id:%d, x:%d, y:%d (vmap rep.: x:%d, y:%d)", GetMapName(), GetId(), x,y, x,y);
            break;
        case VMAP::VMAP_LOAD_RESULT_ERROR:
            sLog.outDetail("Could not load VMAP name:%s, id:%d, x:%d, y:%d (vmap rep.: x:%d, y:%d)", GetMapName(), GetId(), x,y, x,y);
            break;
        case VMAP::VMAP_LOAD_RESULT_IGNORED:
            DEBUG_LOG("Ignored VMAP name:%s, id:%d, x:%d, y:%d (vmap rep.: x:%d, y:%d)", GetMapName(), GetId(), x,y, x,y);
            break;
    }
}

void Map::LoadMap(int gx,int gy, bool reload)
{
    if (i_InstanceId != 0)
    {
        if (GridMaps[gx][gy])
            return;

        Map* baseMap = const_cast<Map*>(sMapMgr.CreateBaseMap(i_id));

        // load gridmap for base map
        if (!baseMap->GridMaps[gx][gy])
            baseMap->EnsureGridCreated(GridPair(63-gx,63-gy));

        ((MapInstanced*)(baseMap))->AddGridMapReference(GridPair(gx,gy));
        GridMaps[gx][gy] = baseMap->GridMaps[gx][gy];
        return;
    }

    if (GridMaps[gx][gy] && !reload)
        return;

    //map already load, delete it before reloading (Is it necessary? Do we really need the ability the reload maps during runtime?)
    if (GridMaps[gx][gy])
    {
        sLog.outDetail("Unloading already loaded map %u before reloading.", GetId());
        delete (GridMaps[gx][gy]);
        GridMaps[gx][gy] = NULL;
    }

    // map file name
    char *tmp = NULL;

    // Pihhan: dataPath length + "maps/" + 3+2+2+ ".map" length may be > 32 !
    int len = sWorld.GetDataPath().length()+strlen("maps/%03u%02u%02u.map")+1;
    tmp = new char[len];
    snprintf(tmp, len, (char *)(sWorld.GetDataPath()+"maps/%03u%02u%02u.map").c_str(), GetId(), gx, gy);
    sLog.outDetail("Loading map %s",tmp);

    // loading data
    GridMaps[gx][gy] = new GridMap();
    if (!GridMaps[gx][gy]->loadData(tmp))
    {
        sLog.outError("Error load map file: \n %s\n", tmp);
    }
    delete [] tmp;
    return;
}

void Map::LoadMapAndVMap(int gx,int gy)
{
    LoadMap(gx,gy);
    if (i_InstanceId == 0)
        LoadVMap(gx, gy);        // Only load the data for the base map
}

void Map::InitStateMachine()
{
    si_GridStates[GRID_STATE_INVALID] = new InvalidState;
    si_GridStates[GRID_STATE_ACTIVE] = new ActiveState;
    si_GridStates[GRID_STATE_IDLE] = new IdleState;
    si_GridStates[GRID_STATE_REMOVAL] = new RemovalState;
}

void Map::DeleteStateMachine()
{
    delete si_GridStates[GRID_STATE_INVALID];
    delete si_GridStates[GRID_STATE_ACTIVE];
    delete si_GridStates[GRID_STATE_IDLE];
    delete si_GridStates[GRID_STATE_REMOVAL];
}

Map::Map(uint32 id, time_t expiry, uint32 InstanceId, uint8 SpawnMode)
   : i_mapEntry (sMapStore.LookupEntry(id)), i_spawnMode(SpawnMode),
     i_id(id), i_InstanceId(InstanceId), m_unloadTimer(0), i_gridExpiry(expiry),
     m_VisibleDistance(DEFAULT_VISIBILITY_DISTANCE), m_activeNonPlayersIter(m_activeNonPlayers.end()), i_scriptLock(true),
     m_VisibilityNotifyPeriod(DEFAULT_VISIBILITY_NOTIFY_PERIOD)
{
    for (unsigned int idx=0; idx < MAX_NUMBER_OF_GRIDS; ++idx)
    {
        for (unsigned int j=0; j < MAX_NUMBER_OF_GRIDS; ++j)
        {
            //z code
            GridMaps[idx][j] =NULL;
            setNGrid(NULL, idx, j);
        }
    }

    m_VisibilityNotifyPeriod = World::GetVisibilityNotifyPeriodOnContinents();

    //lets initialize visibility distance for map
    Map::InitVisibilityDistance();
    SetBroken(false);
}

void Map::InitVisibilityDistance()
{
    //init visibility for continents
    m_VisibleDistance = sWorld.GetMaxVisibleDistanceOnContinents();
}

// Template specialization of utility methods
template<class T>
void Map::AddToGrid(T* obj, NGridType *grid, Cell const& cell)
{
    (*grid)(cell.CellX(), cell.CellY()).template AddGridObject<T>(obj);
}

template<>
void Map::AddToGrid(Player* obj, NGridType *grid, Cell const& cell)
{
    (*grid)(cell.CellX(), cell.CellY()).AddWorldObject(obj);
}

template<>
void Map::AddToGrid(Corpse *obj, NGridType *grid, Cell const& cell)
{
    // add to world object registry in grid
    if (obj->GetType()!=CORPSE_BONES)
    {
        (*grid)(cell.CellX(), cell.CellY()).AddWorldObject(obj);
    }
    // add to grid object store
    else
    {
        (*grid)(cell.CellX(), cell.CellY()).AddGridObject(obj);
    }
}

template<>
void Map::AddToGrid(Creature* obj, NGridType *grid, Cell const& cell)
{
    // add to world object registry in grid
    if (obj->isPet() || obj->IsTempWorldObject)
    {
        (*grid)(cell.CellX(), cell.CellY()).AddWorldObject<Creature>(obj);
    }
    // add to grid object store
    else
    {
        (*grid)(cell.CellX(), cell.CellY()).AddGridObject<Creature>(obj);
    }
    obj->SetCurrentCell(cell);
}

template<>
void Map::AddToGrid(DynamicObject* obj, NGridType *grid, Cell const& cell)
{
    if (obj->isActiveObject()) // only farsight
        (*grid)(cell.CellX(), cell.CellY()).AddWorldObject<DynamicObject>(obj);
    else
        (*grid)(cell.CellX(), cell.CellY()).AddGridObject<DynamicObject>(obj);
}

template<class T>
void Map::RemoveFromGrid(T* obj, NGridType *grid, Cell const& cell)
{
    (*grid)(cell.CellX(), cell.CellY()).template RemoveGridObject<T>(obj);
}

template<>
void Map::RemoveFromGrid(Player* obj, NGridType *grid, Cell const& cell)
{
    (*grid)(cell.CellX(), cell.CellY()).RemoveWorldObject(obj);
}

template<>
void Map::RemoveFromGrid(Corpse *obj, NGridType *grid, Cell const& cell)
{
    // remove from world object registry in grid
    if (obj->GetType()!=CORPSE_BONES)
    {
        (*grid)(cell.CellX(), cell.CellY()).RemoveWorldObject(obj);
    }
    // remove from grid object store
    else
    {
        (*grid)(cell.CellX(), cell.CellY()).RemoveGridObject(obj);
    }
}

template<>
void Map::RemoveFromGrid(Creature* obj, NGridType *grid, Cell const& cell)
{
    // remove from world object registry in grid
    if (obj->isPet() || obj->IsTempWorldObject)
    {
        (*grid)(cell.CellX(), cell.CellY()).RemoveWorldObject<Creature>(obj);
    }
    // remove from grid object store
    else
    {
        (*grid)(cell.CellX(), cell.CellY()).RemoveGridObject<Creature>(obj);
    }
}

template<>
void Map::RemoveFromGrid(DynamicObject* obj, NGridType *grid, Cell const& cell)
{
    if (obj->isActiveObject()) // only farsight
        (*grid)(cell.CellX(), cell.CellY()).RemoveWorldObject<DynamicObject>(obj);
    else
        (*grid)(cell.CellX(), cell.CellY()).RemoveGridObject<DynamicObject>(obj);
}

template<class T>
void Map::SwitchGridContainers(T* obj, bool on)
{
    CellPair p = Trinity::ComputeCellPair(obj->GetPositionX(), obj->GetPositionY());
    if (p.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || p.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
    {
        sLog.outError("Map::SwitchGridContainers: Object " I64FMT " have invalid coordinates X:%f Y:%f grid cell [%u:%u]", obj->GetGUID(), obj->GetPositionX(), obj->GetPositionY(), p.x_coord, p.y_coord);
        return;
    }

    Cell cell(p);
    if (!loaded(GridPair(cell.data.Part.grid_x, cell.data.Part.grid_y)))
        return;

    DEBUG_LOG("Switch object " I64FMT " from grid[%u,%u] %u", obj->GetGUID(), cell.data.Part.grid_x, cell.data.Part.grid_y, on);
    NGridType *ngrid = getNGrid(cell.GridX(), cell.GridY());
    assert(ngrid != NULL);

    GridType &grid = (*ngrid)(cell.CellX(), cell.CellY());

    if (on)
    {
        grid.RemoveGridObject<T>(obj);
        grid.AddWorldObject<T>(obj);
    }
    else
    {
        grid.RemoveWorldObject<T>(obj);
        grid.AddGridObject<T>(obj);
    }
    obj->IsTempWorldObject = on;
}

template void Map::SwitchGridContainers(Creature *, bool);
template void Map::SwitchGridContainers(DynamicObject *, bool);

template<class T>
void Map::DeleteFromWorld(T* obj)
{
    // Note: In case resurrectable corpse and pet its removed from global lists in own destructor
    delete obj;
}

void Map::EnsureGridCreated(const GridPair &p)
{
    if (!getNGrid(p.x_coord, p.y_coord))
    {
        Guard guard(*this);
        if (!getNGrid(p.x_coord, p.y_coord))
        {
            sLog.outDebug("Loading grid[%u,%u] for map %u", p.x_coord, p.y_coord, i_id);

            setNGrid(new NGridType(p.x_coord*MAX_NUMBER_OF_GRIDS + p.y_coord, p.x_coord, p.y_coord, i_gridExpiry, sWorld.getConfig(CONFIG_GRID_UNLOAD)),
                p.x_coord, p.y_coord);

            // build a linkage between this map and NGridType
            buildNGridLinkage(getNGrid(p.x_coord, p.y_coord));

            getNGrid(p.x_coord, p.y_coord)->SetGridState(GRID_STATE_IDLE);

            //z coord
            int gx = (MAX_NUMBER_OF_GRIDS - 1) - p.x_coord;
            int gy = (MAX_NUMBER_OF_GRIDS - 1) - p.y_coord;

            if (!GridMaps[gx][gy])
                LoadMapAndVMap(gx,gy);
        }
    }
}

void Map::EnsureGridLoadedAtEnter(const Cell &cell, Player *player)
{
    EnsureGridLoaded(cell);
    NGridType *grid = getNGrid(cell.GridX(), cell.GridY());
    ASSERT(grid != NULL);


    // refresh grid state & timer
    if (grid->GetGridState() != GRID_STATE_ACTIVE)
    {
        ResetGridExpiry(*grid, 0.1f);;
        grid->SetGridState(GRID_STATE_ACTIVE);
    }
}

bool Map::EnsureGridLoaded(const Cell &cell)
{
    EnsureGridCreated(GridPair(cell.GridX(), cell.GridY()));
    NGridType *grid = getNGrid(cell.GridX(), cell.GridY());

    assert(grid != NULL);
    if (!isGridObjectDataLoaded(cell.GridX(), cell.GridY()))
    {
        sLog.outDebug("Loading grid[%u,%u] for map %u instance %u", cell.GridX(), cell.GridY(), GetId(), i_InstanceId);

        ObjectGridLoader loader(*grid, this, cell);
        loader.LoadN();

        // Add resurrectable corpses to world object list in grid
        ObjectAccessor::Instance().AddCorpsesToGrid(GridPair(cell.GridX(),cell.GridY()),(*grid)(cell.CellX(), cell.CellY()), this);

        setGridObjectDataLoaded(true,cell.GridX(), cell.GridY());
        return true;
    }
    return false;
}

void Map::LoadGrid(float x, float y)
{
    CellPair pair = Trinity::ComputeCellPair(x, y);
    Cell cell(pair);
    EnsureGridLoaded(cell);
}

bool Map::Add(Player *player)
{
    player->SetInstanceId(GetInstanceId());

    CellPair p = Trinity::ComputeCellPair(player->GetPositionX(), player->GetPositionY());
    if(p.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || p.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP )
    {
        sLog.outError("Map::Add: Player (GUID: %u) have invalid coordinates X:%f Y:%f grid cell [%u:%u]", player->GetGUIDLow(), player->GetPositionX(), player->GetPositionY(), p.x_coord, p.y_coord);
        return false;
    }

    player->GetMapRef().link(this, player);

    Cell cell(p);
    EnsureGridLoadedAtEnter(cell, player);
    NGridType *grid = getNGrid(cell.GridX(), cell.GridY());
    ASSERT(grid != NULL);
    AddToGrid(player, grid, cell);

    player->AddToWorld();

    SendInitSelf(player);
    SendInitTransports(player);

    player->m_clientGUIDs.clear();
    player->UpdateObjectVisibility(true);

    return true;
}

template<class T>
void
Map::Add(T *obj)
{
    CellPair p = Trinity::ComputeCellPair(obj->GetPositionX(), obj->GetPositionY());

    if (p.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || p.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
    {
        sLog.outError("Map::Add: Object " UI64FMTD " have invalid coordinates X:%f Y:%f grid cell [%u:%u]", obj->GetGUID(), obj->GetPositionX(), obj->GetPositionY(), p.x_coord, p.y_coord);
        return;
    }

    Cell cell(p);
    if (obj->IsInWorld()) // need some clean up later
    {
        obj->UpdateObjectVisibility(true);
        return;
    }

    if (obj->isActiveObject())
        EnsureGridLoadedAtEnter(cell);
    else
        EnsureGridCreated(GridPair(cell.GridX(), cell.GridY()));

    NGridType *grid = getNGrid(cell.GridX(), cell.GridY());
    assert(grid != NULL);

    AddToGrid(obj,grid,cell);
    obj->AddToWorld();

    if (obj->isActiveObject())
        AddToActive(obj);

    DEBUG_LOG("Object %u enters grid[%u,%u]", GUID_LOPART(obj->GetGUID()), cell.GridX(), cell.GridY());

    //trigger needs to cast spell, if not update, cannot see visual
    obj->UpdateObjectVisibility(true);
}

void Map::MessageBroadcast(Player *player, WorldPacket *msg, bool to_self, bool to_possessor)
{
    Trinity::MessageDeliverer post_man(*player, msg, to_possessor, to_self);
    Cell::VisitWorldObjects(player, post_man, GetVisibilityDistance());
}

void Map::MessageBroadcast(WorldObject *obj, WorldPacket *msg, bool to_possessor)
{
    Trinity::ObjectMessageDeliverer post_man(*obj, msg, to_possessor);
    Cell::VisitWorldObjects(obj, post_man, GetVisibilityDistance(obj));
}

void Map::MessageDistBroadcast(Player *player, WorldPacket *msg, float dist, bool to_self, bool to_possessor, bool own_team_only)
{
    Trinity::MessageDistDeliverer post_man(*player, msg, to_possessor, dist, to_self, own_team_only);
    Cell::VisitWorldObjects(player, post_man, GetVisibilityDistance());
}

void Map::MessageDistBroadcast(WorldObject *obj, WorldPacket *msg, float dist, bool to_possessor)
{
    Trinity::ObjectMessageDistDeliverer post_man(*obj, msg, to_possessor, dist);
    Cell::VisitWorldObjects(obj, post_man, GetVisibilityDistance());
}

bool Map::loaded(const GridPair &p) const
{
    return (getNGrid(p.x_coord, p.y_coord) && isGridObjectDataLoaded(p.x_coord, p.y_coord));
}

void Map::Update(const uint32 &t_diff)
{
    /// update worldsessions for existing players
    for (m_mapRefIter = m_mapRefManager.begin(); m_mapRefIter != m_mapRefManager.end(); ++m_mapRefIter)
    {
        Player* plr = m_mapRefIter->getSource();
        if (plr && plr->IsInWorld())
        {
            WorldSession * pSession = plr->GetSession();
            MapSessionFilter updater(pSession);
            pSession->Update(t_diff, updater);
        }
    }

    /// update players at tick
    for (m_mapRefIter = m_mapRefManager.begin(); m_mapRefIter != m_mapRefManager.end(); ++m_mapRefIter)
    {
        Player* plr = m_mapRefIter->getSource();
        if (plr && plr->IsInWorld())
        {
            WorldObject::UpdateHelper helper(plr);
            helper.Update(t_diff);
        }
    }

    resetMarkedCells();

    Trinity::ObjectUpdater updater(t_diff);
    // for creature
    TypeContainerVisitor<Trinity::ObjectUpdater, GridTypeMapContainer  > grid_object_update(updater);
    // for pets
    TypeContainerVisitor<Trinity::ObjectUpdater, WorldTypeMapContainer > world_object_update(updater);

    // the player iterator is stored in the map object
    // to make sure calls to Map::Remove don't invalidate it
    for (m_mapRefIter = m_mapRefManager.begin(); m_mapRefIter != m_mapRefManager.end(); ++m_mapRefIter)
    {
        Player* plr = m_mapRefIter->getSource();

        if (!plr->IsInWorld() || !plr->IsPositionValid())
            continue;

        CellArea area = Cell::CalculateCellArea(plr->GetPositionX(), plr->GetPositionY(), GetVisibilityDistance());

        for (uint32 x = area.low_bound.x_coord; x < area.high_bound.x_coord; ++x)
        {
            for (uint32 y = area.low_bound.y_coord; y < area.high_bound.y_coord; ++y)
            {
                // marked cells are those that have been visited
                // don't visit the same cell twice
                uint32 cell_id = (y * TOTAL_NUMBER_OF_CELLS_PER_MAP) + x;
                if (!isCellMarked(cell_id))
                {
                    markCell(cell_id);
                    CellPair pair(x,y);
                    Cell cell(pair);
                    cell.SetNoCreate();
                    Visit(cell, grid_object_update);
                    Visit(cell, world_object_update);
                }
            }
        }
    }

    // non-player active objects
    if (!m_activeNonPlayers.empty())
    {
        for (m_activeNonPlayersIter = m_activeNonPlayers.begin(); m_activeNonPlayersIter != m_activeNonPlayers.end();)
        {
            // skip not in world
            WorldObject* obj = *m_activeNonPlayersIter;

            // step before processing, in this case if Map::Remove remove next object we correctly
            // step to next-next, and if we step to end() then newly added objects can wait next update.
            ++m_activeNonPlayersIter;

            if (!obj->IsInWorld() || !obj->IsPositionValid())
                continue;

            CellArea area = Cell::CalculateCellArea(obj->GetPositionX(), obj->GetPositionY(), GetVisibilityDistance());

            for (uint32 x = area.low_bound.x_coord; x < area.high_bound.x_coord; ++x)
            {
                for (uint32 y = area.low_bound.y_coord; y < area.high_bound.y_coord; ++y)
                {
                    // marked cells are those that have been visited
                    // don't visit the same cell twice
                    uint32 cell_id = (y * TOTAL_NUMBER_OF_CELLS_PER_MAP) + x;
                    if (!isCellMarked(cell_id))
                    {
                        markCell(cell_id);
                        CellPair pair(x,y);
                        Cell cell(pair);
                        cell.SetNoCreate();
                        Visit(cell, grid_object_update);
                        Visit(cell, world_object_update);
                    }
                }
            }
        }
    }

    // Send world objects and item update field changes
    SendObjectUpdates();

    ///- Process necessary scripts
    if (!m_scriptSchedule.empty())
    {
        i_scriptLock = true;
        ScriptsProcess();
        i_scriptLock = false;
    }

    MoveAllCreaturesInMoveList();

    if(!m_mapRefManager.isEmpty() || !m_activeNonPlayers.empty())
        ProcessRelocationNotifies(t_diff);
}

struct ResetNotifier
{
    template<class T>inline void resetNotify(GridRefManager<T> &m)
    {
        for(typename GridRefManager<T>::iterator iter=m.begin(); iter != m.end(); ++iter)
            iter->getSource()->ResetAllNotifies();
    }

    template<class T> void Visit(GridRefManager<T> &) {}
    void Visit(CreatureMapType &m) { resetNotify<Creature>(m);}
    void Visit(PlayerMapType &m) { resetNotify<Player>(m);}
};

void Map::ProcessRelocationNotifies(const uint32 & diff)
{
    for(GridRefManager<NGridType>::iterator i = GridRefManager<NGridType>::begin(); i != GridRefManager<NGridType>::end(); ++i)
    {
        NGridType *grid = i->getSource();

        if (grid->GetGridState() != GRID_STATE_ACTIVE)
            continue;

        grid->getGridInfoRef()->getRelocationTimer().TUpdate(diff);
        if (!grid->getGridInfoRef()->getRelocationTimer().TPassed())
            continue;

        uint32 gx = grid->getX(), gy = grid->getY();

        CellPair cell_min(gx*MAX_NUMBER_OF_CELLS, gy*MAX_NUMBER_OF_CELLS);
        CellPair cell_max(cell_min.x_coord + MAX_NUMBER_OF_CELLS, cell_min.y_coord+MAX_NUMBER_OF_CELLS);

        for (uint32 x = cell_min.x_coord; x < cell_max.x_coord; ++x)
        {
            for (uint32 y = cell_min.y_coord; y < cell_max.y_coord; ++y)
            {
                uint32 cell_id = (y * TOTAL_NUMBER_OF_CELLS_PER_MAP) + x;
                if (!isCellMarked(cell_id))
                    continue;

                CellPair pair(x,y);
                Cell cell(pair);
                cell.SetNoCreate();

                Trinity::DelayedUnitRelocation cell_relocation(cell, pair, *this, GetVisibilityDistance());
                TypeContainerVisitor<Trinity::DelayedUnitRelocation, GridTypeMapContainer  > grid_object_relocation(cell_relocation);
                TypeContainerVisitor<Trinity::DelayedUnitRelocation, WorldTypeMapContainer > world_object_relocation(cell_relocation);
                Visit(cell, grid_object_relocation);
                Visit(cell, world_object_relocation);
            }
        }
    }

    ResetNotifier reset;
    TypeContainerVisitor<ResetNotifier, GridTypeMapContainer >  grid_notifier(reset);
    TypeContainerVisitor<ResetNotifier, WorldTypeMapContainer > world_notifier(reset);

    for (GridRefManager<NGridType>::iterator i = GridRefManager<NGridType>::begin(); i != GridRefManager<NGridType>::end(); ++i)
    {
        NGridType *grid = i->getSource();

        if (grid->GetGridState() != GRID_STATE_ACTIVE)
            continue;

        if (!grid->getGridInfoRef()->getRelocationTimer().TPassed())
            continue;

        grid->getGridInfoRef()->getRelocationTimer().TReset(diff, m_VisibilityNotifyPeriod);
        uint32 gx = grid->getX(), gy = grid->getY();

        CellPair cell_min(gx*MAX_NUMBER_OF_CELLS, gy*MAX_NUMBER_OF_CELLS);
        CellPair cell_max(cell_min.x_coord + MAX_NUMBER_OF_CELLS, cell_min.y_coord+MAX_NUMBER_OF_CELLS);

        for (uint32 x = cell_min.x_coord; x < cell_max.x_coord; ++x)
        {
            for (uint32 y = cell_min.y_coord; y < cell_max.y_coord; ++y)
            {
                uint32 cell_id = (y * TOTAL_NUMBER_OF_CELLS_PER_MAP) + x;
                if (!isCellMarked(cell_id))
                    continue;

                CellPair pair(x,y);
                Cell cell(pair);
                cell.SetNoCreate();
                Visit(cell, grid_notifier);
                Visit(cell, world_notifier);
            }
        }
    }
}

void Map::SendObjectUpdates()
{
    UpdateDataMapType update_players;
    for (std::set<Object*>::const_iterator it = i_objectsToClientUpdate.begin();it!= i_objectsToClientUpdate.end();++it)
    {
        if (*it && (*it)->IsInWorld())
            (*it)->BuildUpdate(update_players);
    }

    i_objectsToClientUpdate.clear();

    WorldPacket packet;                                     // here we allocate a std::vector with a size of 0x10000
    for (UpdateDataMapType::iterator iter = update_players.begin(); iter != update_players.end(); ++iter)
    {
        if (iter->second.BuildPacket(&packet))
            iter->first->GetSession()->SendPacket(&packet);
        packet.clear();                                     // clean the string
    }
}

void Map::Remove(Player *player, bool remove)
{
    // this may be called during Map::Update
    // after decrement+unlink, ++m_mapRefIter will continue correctly
    // when the first element of the list is being removed
    // nocheck_prev will return the padding element of the RefManager
    // instead of NULL in the case of prev
    if (m_mapRefIter == player->GetMapRef())
        m_mapRefIter = m_mapRefIter->nocheck_prev();

    player->GetMapRef().unlink();
    CellPair p = Trinity::ComputeCellPair(player->GetPositionX(), player->GetPositionY());
    if (p.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || p.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
    {
        // invalid coordinates
        player->RemoveFromWorld();

        if (remove)
            DeleteFromWorld(player);

        return;
    }

    Cell cell(p);

    if (!getNGrid(cell.data.Part.grid_x, cell.data.Part.grid_y))
    {
        sLog.outError("Map::Remove() i_grids was NULL x:%d, y:%d",cell.data.Part.grid_x,cell.data.Part.grid_y);
        return;
    }

    DEBUG_LOG("Remove player %s from grid[%u,%u]", player->GetName(), cell.GridX(), cell.GridY());
    NGridType *grid = getNGrid(cell.GridX(), cell.GridY());
    assert(grid != NULL);

    player->UpdateObjectVisibility(true);
    player->RemoveFromWorld();
    RemoveFromGrid(player,grid,cell);

    SendRemoveTransports(player);

    if (remove)
        DeleteFromWorld(player);
}

bool Map::RemoveBones(uint64 guid, float x, float y)
{
    if (IsRemovalGrid(x, y))
    {
        Corpse * corpse = ObjectAccessor::GetCorpse(this->GetId() ,x, y, guid);
        if (corpse && corpse->GetTypeId() == TYPEID_CORPSE && corpse->GetType() == CORPSE_BONES)
            corpse->DeleteBonesFromWorld();
        else
            return false;
    }
    return true;
}

template<class T>
void Map::Remove(T *obj, bool remove)
{
    CellPair p = Trinity::ComputeCellPair(obj->GetPositionX(), obj->GetPositionY());
    if (p.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || p.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
    {
        sLog.outError("Map::Remove: Object " I64FMT " have invalid coordinates X:%f Y:%f grid cell [%u:%u]", obj->GetGUID(), obj->GetPositionX(), obj->GetPositionY(), p.x_coord, p.y_coord);
        return;
    }

    Cell cell(p);
    if (!loaded(GridPair(cell.data.Part.grid_x, cell.data.Part.grid_y)))
        return;

    DEBUG_LOG("Remove object " I64FMT " from grid[%u,%u]", obj->GetGUID(), cell.data.Part.grid_x, cell.data.Part.grid_y);
    NGridType *grid = getNGrid(cell.GridX(), cell.GridY());
    assert(grid != NULL);

    obj->RemoveFromWorld();
    if (obj->isActiveObject())
        RemoveFromActive(obj);
    obj->UpdateObjectVisibility(true);
    RemoveFromGrid(obj,grid,cell);

    if (remove)
    {
        // if option set then object already saved at this moment
        if (!sWorld.getConfig(CONFIG_SAVE_RESPAWN_TIME_IMMEDIATELY))
            obj->SaveRespawnTime();
        DeleteFromWorld(obj);
    }
}

void Map::PlayerRelocation(Player *player, float x, float y, float z, float orientation)
{
    assert(player);

    CellPair old_val = Trinity::ComputeCellPair(player->GetPositionX(), player->GetPositionY());
    CellPair new_val = Trinity::ComputeCellPair(x, y);

    Cell old_cell(old_val);
    Cell new_cell(new_val);

    player->Relocate(x, y, z, orientation);

    if (old_cell.DiffGrid(new_cell) || old_cell.DiffCell(new_cell))
    {
        DEBUG_LOG("Player %s relocation grid[%u,%u]cell[%u,%u]->grid[%u,%u]cell[%u,%u]", player->GetName(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY(), new_cell.GridX(), new_cell.GridY(), new_cell.CellX(), new_cell.CellY());

        // update player position for group at taxi flight
        if (player->GetGroup() && player->IsTaxiFlying())
            player->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_POSITION);

        NGridType* oldGrid = getNGrid(old_cell.GridX(), old_cell.GridY());
        RemoveFromGrid(player, oldGrid,old_cell);
        if (old_cell.DiffGrid(new_cell))
            EnsureGridLoadedAtEnter(new_cell, player);

        NGridType* newGrid = getNGrid(new_cell.GridX(), new_cell.GridY());
        AddToGrid(player, newGrid,new_cell);
    }

    player->UpdateObjectVisibility(player->GetMotionMaster()->GetCurrentMovementGeneratorType() != IDLE_MOTION_TYPE);
}

void Map::CreatureRelocation(Creature *creature, float x, float y, float z, float ang)
{
    assert(CheckGridIntegrity(creature,false));

    Cell old_cell = creature->GetCurrentCell();

    CellPair new_val = Trinity::ComputeCellPair(x, y);
    Cell new_cell(new_val);

    // delay creature move for grid/cell to grid/cell moves
    if (old_cell.DiffCell(new_cell) || old_cell.DiffGrid(new_cell))
    {
        #ifdef TRINITY_DEBUG
        if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
            sLog.outDebug("Creature (GUID: %u Entry: %u) added to moving list from grid[%u,%u]cell[%u,%u] to grid[%u,%u]cell[%u,%u].", creature->GetGUIDLow(), creature->GetEntry(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY(), new_cell.GridX(), new_cell.GridY(), new_cell.CellX(), new_cell.CellY());
        #endif
        AddCreatureToMoveList(creature,x,y,z,ang);
        // in diffcell/diffgrid case notifiers called at finishing move creature in Map::MoveAllCreaturesInMoveList
    }
    else
    {
        creature->Relocate(x, y, z, ang);
        creature->UpdateObjectVisibility(false);
    }
    assert(CheckGridIntegrity(creature,true));
}

void Map::AddCreatureToMoveList(Creature *c, float x, float y, float z, float ang)
{
    if (!c)
        return;

    i_creaturesToMove[c] = CreatureMover(x,y,z,ang);
}

void Map::MoveAllCreaturesInMoveList()
{
    while (!i_creaturesToMove.empty())
    {
        // get data and remove element;
        CreatureMoveList::iterator iter = i_creaturesToMove.begin();
        Creature* c = iter->first;
        CreatureMover cm = iter->second;
        i_creaturesToMove.erase(iter);

        // calculate cells
        CellPair new_val = Trinity::ComputeCellPair(cm.x, cm.y);
        Cell new_cell(new_val);

        // do move or do move to respawn or remove creature if previous all fail
        if (CreatureCellRelocation(c,new_cell))
        {
            // update pos
            c->Relocate(cm.x, cm.y, cm.z, cm.ang);
            //CreatureRelocationNotify(c,new_cell,new_cell.cellPair());
            c->UpdateObjectVisibility(false);
        }
        else
        {
            // if creature can't be move in new cell/grid (not loaded) move it to repawn cell/grid
            // creature coordinates will be updated and notifiers send
            if (!CreatureRespawnRelocation(c))
            {
                // ... or unload (if respawn grid also not loaded)
                #ifdef TRINITY_DEBUG
                if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
                    sLog.outDebug("Creature (GUID: %u Entry: %u) can't be move to unloaded respawn grid.",c->GetGUIDLow(),c->GetEntry());
                #endif
                AddObjectToRemoveList(c);
            }
        }
    }
}

bool Map::CreatureCellRelocation(Creature *c, Cell new_cell)
{
    Cell const& old_cell = c->GetCurrentCell();
    if (!old_cell.DiffGrid(new_cell))                       // in same grid
    {
        // if in same cell then none do
        if (old_cell.DiffCell(new_cell))
        {
            #ifdef TRINITY_DEBUG
            if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
                sLog.outDebug("Creature (GUID: %u Entry: %u) moved in grid[%u,%u] from cell[%u,%u] to cell[%u,%u].", c->GetGUIDLow(), c->GetEntry(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY(), new_cell.CellX(), new_cell.CellY());
            #endif

            RemoveFromGrid(c,getNGrid(old_cell.GridX(), old_cell.GridY()),old_cell);
            AddToGrid(c,getNGrid(new_cell.GridX(), new_cell.GridY()),new_cell);
        }
        else
        {
            #ifdef TRINITY_DEBUG
            if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
                sLog.outDebug("Creature (GUID: %u Entry: %u) move in same grid[%u,%u]cell[%u,%u].", c->GetGUIDLow(), c->GetEntry(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY());
            #endif
        }

        return true;
    }

    // in diff. grids but active creature
    if (c->isActiveObject())
    {
        EnsureGridLoadedAtEnter(new_cell);

        #ifdef TRINITY_DEBUG
        if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
            sLog.outDebug("Active creature (GUID: %u Entry: %u) moved from grid[%u,%u]cell[%u,%u] to grid[%u,%u]cell[%u,%u].", c->GetGUIDLow(), c->GetEntry(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY(), new_cell.GridX(), new_cell.GridY(), new_cell.CellX(), new_cell.CellY());
        #endif

        RemoveFromGrid(c,getNGrid(old_cell.GridX(), old_cell.GridY()),old_cell);
        AddToGrid(c,getNGrid(new_cell.GridX(), new_cell.GridY()),new_cell);

        return true;
    }

    // in diff. loaded grid normal creature
    if (loaded(GridPair(new_cell.GridX(), new_cell.GridY())))
    {
        #ifdef TRINITY_DEBUG
        if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
            sLog.outDebug("Creature (GUID: %u Entry: %u) moved from grid[%u,%u]cell[%u,%u] to grid[%u,%u]cell[%u,%u].", c->GetGUIDLow(), c->GetEntry(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY(), new_cell.GridX(), new_cell.GridY(), new_cell.CellX(), new_cell.CellY());
        #endif

        RemoveFromGrid(c,getNGrid(old_cell.GridX(), old_cell.GridY()),old_cell);
        EnsureGridCreated(GridPair(new_cell.GridX(), new_cell.GridY()));
        AddToGrid(c,getNGrid(new_cell.GridX(), new_cell.GridY()),new_cell);

        return true;
    }

    // fail to move: normal creature attempt move to unloaded grid
    #ifdef TRINITY_DEBUG
    if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
        sLog.outDebug("Creature (GUID: %u Entry: %u) attempt move from grid[%u,%u]cell[%u,%u] to unloaded grid[%u,%u]cell[%u,%u].", c->GetGUIDLow(), c->GetEntry(), old_cell.GridX(), old_cell.GridY(), old_cell.CellX(), old_cell.CellY(), new_cell.GridX(), new_cell.GridY(), new_cell.CellX(), new_cell.CellY());
    #endif
    return false;
}

bool Map::CreatureRespawnRelocation(Creature *c)
{
    float resp_x, resp_y, resp_z, resp_o;
    c->GetRespawnCoord(resp_x, resp_y, resp_z, &resp_o);

    CellPair resp_val = Trinity::ComputeCellPair(resp_x, resp_y);
    Cell resp_cell(resp_val);

    c->CombatStop();
    c->GetMotionMaster()->Clear();

    #ifdef TRINITY_DEBUG
    if ((sLog.getLogFilter() & LOG_FILTER_CREATURE_MOVES)==0)
        sLog.outDebug("Creature (GUID: %u Entry: %u) will moved from grid[%u,%u]cell[%u,%u] to respawn grid[%u,%u]cell[%u,%u].", c->GetGUIDLow(), c->GetEntry(), c->GetCurrentCell().GridX(), c->GetCurrentCell().GridY(), c->GetCurrentCell().CellX(), c->GetCurrentCell().CellY(), resp_cell.GridX(), resp_cell.GridY(), resp_cell.CellX(), resp_cell.CellY());
    #endif

    // teleport it to respawn point (like normal respawn if player see)
    if (CreatureCellRelocation(c,resp_cell))
    {
        c->Relocate(resp_x, resp_y, resp_z, resp_o);
        c->GetMotionMaster()->Initialize();                 // prevent possible problems with default move generators
        //CreatureRelocationNotify(c,resp_cell,resp_cell.cellPair());
        c->UpdateObjectVisibility(false);
        return true;
    }
    else
        return false;
}

bool Map::UnloadGrid(const uint32 &x, const uint32 &y, bool unloadAll)
{
    NGridType *grid = getNGrid(x, y);
    assert(grid != NULL);

    {
        if (!unloadAll && ActiveObjectsNearGrid(x, y))
             return false;

        sLog.outDebug("Unloading grid[%u,%u] for map %u", x,y, i_id);

        ObjectGridUnloader unloader(*grid);

        if (!unloadAll)
        {
            // Finish creature moves, remove and delete all creatures with delayed remove before moving to respawn grids
            // Must know real mob position before move
            MoveAllCreaturesInMoveList();

            // move creatures to respawn grids if this is diff.grid or to remove list
            unloader.MoveToRespawnN();

            // Finish creature moves, remove and delete all creatures with delayed remove before unload
            MoveAllCreaturesInMoveList();
        }

        ObjectGridCleaner cleaner(*grid);
        cleaner.CleanN();

        RemoveAllObjectsInRemoveList();

        unloader.UnloadN();

        assert(i_objectsToRemove.empty());

        delete grid;
        setNGrid(NULL, x, y);
    }
    int gx = (MAX_NUMBER_OF_GRIDS - 1) - x;
    int gy = (MAX_NUMBER_OF_GRIDS - 1) - y;

    // delete grid map, but don't delete if it is from parent map (and thus only reference)
    //+++if (GridMaps[gx][gy]) don't check for GridMaps[gx][gy], we might have to unload vmaps
    {
        if (i_InstanceId == 0)
        {
            if (GridMaps[gx][gy])
            {
                GridMaps[gx][gy]->unloadData();
                delete GridMaps[gx][gy];
            }

            VMAP::VMapFactory::createOrGetVMapManager()->unloadMap(GetId(), gx, gy);
        }
        else
            ((MapInstanced*)(sMapMgr.CreateBaseMap(i_id)))->RemoveGridMapReference(GridPair(gx, gy));
        GridMaps[gx][gy] = NULL;
    }
    DEBUG_LOG("Unloading grid[%u,%u] for map %u finished", x,y, i_id);
    return true;
}

void Map::UnloadAll()
{
    // clear all delayed moves, useless anyway do this moves before map unload.
    i_creaturesToMove.clear();

    for (GridRefManager<NGridType>::iterator i = GridRefManager<NGridType>::begin(); i != GridRefManager<NGridType>::end();)
    {
        NGridType &grid(*i->getSource());
        ++i;
        UnloadGrid(grid.getX(), grid.getY(), true);       // deletes the grid and removes it from the GridRefManager
    }
}

bool Map::hasVMapHeight()
{
    VMAP::IVMapManager *vMgr = VMAP::VMapFactory::createOrGetVMapManager();
    return vMgr && vMgr->isHeightCalcEnabled(GetId());
}

bool Map::hasPosCollisionCalcEnabled()
{
    VMAP::IVMapManager *vMgr = VMAP::VMapFactory::createOrGetVMapManager();
    return vMgr && vMgr->isPosCollisionCalcEnabled(GetId());
}

float Map::GetHeight(float x, float y, float z, bool pUseVmaps, float maxSearchDist) const
{
    // find raw .map surface under Z coordinates
    float mapHeight;
    float z2 = z + 2.f;
    if (GridMap *gmap = const_cast<Map*>(this)->GetGrid(x, y))
    {
        float _mapheight = gmap->getHeight(x,y);

        // look from a bit higher pos to find the floor, ignore under surface case
        if (z2 > _mapheight)
            mapHeight = _mapheight;
        else
            mapHeight = VMAP_INVALID_HEIGHT_VALUE;
    }
    else
        mapHeight = VMAP_INVALID_HEIGHT_VALUE;

    float vmapHeight;
    if (pUseVmaps)
    {
        VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();
        if (vmgr->isHeightCalcEnabled(GetId()))
        {
            // if mapHeight has been found search vmap height at least until mapHeight point
            // this prevent case when original Z "too high above ground and vmap height search fail"
            // this will not affect most normal cases (no map in instance, or stay at ground at continent)
            if (mapHeight > INVALID_HEIGHT && z2 - mapHeight > maxSearchDist)
                maxSearchDist = z2 - mapHeight + 1.0f;      // 1.0 make sure that we not fail for case when map height near but above for vamp height

            // look from a bit higher pos to find the floor
            vmapHeight = vmgr->getHeight(GetId(), x, y, z2, maxSearchDist);
        }
        else
            vmapHeight = VMAP_INVALID_HEIGHT_VALUE;
    }
    else
        vmapHeight = VMAP_INVALID_HEIGHT_VALUE;

    // mapHeight set for any above raw ground Z or <= INVALID_HEIGHT
    // vmapheight set for any under Z value or <= INVALID_HEIGHT
    if (vmapHeight > INVALID_HEIGHT)
    {
        if (mapHeight > INVALID_HEIGHT)
        {
            // we have mapheight and vmapheight and must select more appropriate
            // we are already under the surface or vmap height above map heigt
            // or if the distance of the vmap height is less the land height distance
            if (z < mapHeight || vmapHeight > mapHeight || fabs(mapHeight-z) > fabs(vmapHeight-z))
                return vmapHeight;
            else
                return mapHeight;                           // better use .map surface height
        }
        else
            return vmapHeight;                              // we have only vmapHeight (if have)
    }

    if ((((Map*)this)->hasVMapHeight() && pUseVmaps) || !pUseVmaps)
        return mapHeight;
    else
    {
        if (mapHeight > INVALID_HEIGHT && (z < mapHeight + 2 || z == MAX_HEIGHT))
            return mapHeight;                               // explicitly use map data if original z < mapHeight but map found (z+2 > mapHeight)
        else
            return VMAP_INVALID_HEIGHT_VALUE;               // we not have any height
    }
}

float Map::GetWaterLevel(float x, float y, float z, float* pGround /*= NULL*/) const
{
    if (const_cast<Map*>(this)->GetGrid(x, y))
    {
        // we need ground level (including grid height version) for proper return water level in point
        float ground_z = GetHeight(x, y, z, true, DEFAULT_WATER_SEARCH);
        if (pGround)
            *pGround = ground_z;

        GridMapLiquidData liquid_status;

        GridMapLiquidStatus res = getLiquidStatus(x, y, ground_z, MAP_ALL_LIQUIDS, &liquid_status);
        if (!res)
            return VMAP_INVALID_HEIGHT_VALUE;

        return liquid_status.level;
    }

    return VMAP_INVALID_HEIGHT_VALUE;
}

float Map::GetWaterOrGroundLevel(float x, float y, float z, float* pGround /*= NULL*/, bool swim /*= false*/) const
{
    if (const_cast<Map*>(this)->GetGrid(x, y))
    {
        // we need ground level (including grid height version) for proper return water level in point
        float ground_z = GetHeight(x, y, z, true, DEFAULT_WATER_SEARCH);
        if (pGround)
            *pGround = ground_z;

        GridMapLiquidData liquid_status;

        GridMapLiquidStatus res = getLiquidStatus(x, y, ground_z, MAP_ALL_LIQUIDS, &liquid_status);
        return res ? (swim ? liquid_status.level - 2.0f : liquid_status.level) : ground_z;
    }

    return VMAP_INVALID_HEIGHT_VALUE;
}

inline bool IsOutdoorWMO(uint32 mogpFlags, uint32 mapid, int32 adtId, int32 rootId, int32 groupId, WMOAreaTableEntry const* wmoEntry, AreaTableEntry const* atEntry)
{
    bool outdoor = true;

    if (wmoEntry && atEntry)
    {
        if (atEntry->flags & AREA_FLAG_OUTSIDE)
            return true;
        if (atEntry->flags & AREA_FLAG_INSIDE)
            return false;
    }

    MapEntry const* mapEntry = sMapStore.LookupEntry(mapid);
    outdoor = (mogpFlags & 0x8000) || (mapEntry->Expansion() && mogpFlags & 0x8);

    if (wmoEntry)
    {
        if (wmoEntry->Flags & 4)
            return true;
        if ((wmoEntry->Flags & 2)!=0)
            outdoor = false;
    }

    return outdoor;
}

bool Map::IsOutdoors(float x, float y, float z) const
{
    uint32 mogpFlags;
    int32 adtId, rootId, groupId;

    // no wmo found? -> outside by default
    if (!GetAreaInfo(x, y, z, mogpFlags, adtId, rootId, groupId))
        return true;

    AreaTableEntry const* atEntry = 0;
    WMOAreaTableEntry const* wmoEntry = 0;/*GetWMOAreaTableEntryByTripple(rootId, adtId, groupId);
    if (wmoEntry)
    {
        DEBUG_LOG("Got WMOAreaTableEntry! flag %u, areaid %u", wmoEntry->Flags, wmoEntry->areaId);
        atEntry = GetAreaEntryByAreaID(wmoEntry->areaId);
    }*/

    return IsOutdoorWMO(mogpFlags, i_id, adtId, rootId, groupId, wmoEntry, atEntry);
}

bool Map::GetAreaInfo(float x, float y, float z, uint32 &flags, int32 &adtId, int32 &rootId, int32 &groupId) const
{
    float vmap_z = z;
    VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();
    if (vmgr->getAreaInfo(GetId(), x, y, vmap_z, flags, adtId, rootId, groupId))
    {
        // check if there's terrain between player height and object height
        if (GridMap *gmap = const_cast<Map*>(this)->GetGrid(x, y))
        {
            float _mapheight = gmap->getHeight(x,y);
            // z + 2.0f condition taken from GetHeight(), not sure if it's such a great choice...
            if (z + 2.0f > _mapheight &&  _mapheight > vmap_z)
                return false;
        }
        return true;
    }
    return false;
}

uint16 Map::GetAreaFlag(float x, float y, float z, bool *isOutdoors) const
{
    uint32 mogpFlags;
    int32 adtId, rootId, groupId;
    WMOAreaTableEntry const* wmoEntry = 0;
    AreaTableEntry const* atEntry = 0;
    bool haveAreaInfo = false;

    if (GetAreaInfo(x, y, z, mogpFlags, adtId, rootId, groupId))
    {
        haveAreaInfo = true;
        if (wmoEntry = GetWMOAreaTableEntryByTripple(rootId, adtId, groupId))
            atEntry = GetAreaEntryByAreaID(wmoEntry->areaId);
    }

    uint16 areaflag;
    if (atEntry)
        areaflag = atEntry->exploreFlag;
    else
    {
        if (GridMap *gmap = const_cast<Map*>(this)->GetGrid(x, y))
            areaflag = gmap->getArea(x, y);
        // this used while not all *.map files generated (instances)
        else
            areaflag = GetAreaFlagByMapId(i_id);
    }

    if (isOutdoors)
    {
        if (haveAreaInfo)
            *isOutdoors = IsOutdoorWMO(mogpFlags, i_id, adtId, rootId, groupId, wmoEntry, atEntry);
        else
            *isOutdoors = true;
    }
    return areaflag;
}

uint8 Map::GetTerrainType(float x, float y) const
{
    if (GridMap *gmap = const_cast<Map*>(this)->GetGrid(x, y))
        return gmap->getTerrainType(x, y);
    else
        return 0;
}

GridMapLiquidStatus Map::getLiquidStatus(float x, float y, float z, uint8 ReqLiquidType, GridMapLiquidData *data) const
{
    GridMapLiquidStatus result = LIQUID_MAP_NO_WATER;
    VMAP::IVMapManager* vmgr = VMAP::VMapFactory::createOrGetVMapManager();

    float liquid_level, ground_level = INVALID_HEIGHT;

    uint32 liquid_type;
    if (vmgr->GetLiquidLevel(GetId(), x, y, z, ReqLiquidType, liquid_level, ground_level, liquid_type))
    {
        DEBUG_LOG("getLiquidStatus(): vmap liquid level: %f ground: %f type: %u", liquid_level, ground_level, liquid_type);
        // Check water level and ground level
        if (liquid_level > ground_level && z > ground_level - 2)
        {
            // All ok in water -> store data
            if (data)
            {
                data->type  = liquid_type;
                data->level = liquid_level;
                data->depth_level = ground_level;
            }

            // For speed check as int values
            int delta = int((liquid_level - z) * 10);

            // Get position delta
            if (delta > 20)                   // Under water
                return LIQUID_MAP_UNDER_WATER;
            if (delta > 0)                   // In water
                return LIQUID_MAP_IN_WATER;
            if (delta > -1)                   // Walk on water
                return LIQUID_MAP_WATER_WALK;
            result = LIQUID_MAP_ABOVE_WATER;
        }
    }

    if (GridMap* gmap = const_cast<Map*>(this)->GetGrid(x, y))
    {
        GridMapLiquidData map_data;
        GridMapLiquidStatus map_result = gmap->getLiquidStatus(x, y, z, ReqLiquidType, &map_data);
        // Not override LIQUID_MAP_ABOVE_WATER with LIQUID_MAP_NO_WATER:
        if (map_result != LIQUID_MAP_NO_WATER && (map_data.level > ground_level))
        {
            if (data)
                *data = map_data;
            return map_result;
        }
    }
    return result;
}

uint32 Map::GetAreaId(uint16 areaflag,uint32 map_id)
{
    AreaTableEntry const *entry = GetAreaEntryByAreaFlagAndMap(areaflag,map_id);

    if (entry)
        return entry->ID;
    else
        return 0;
}

uint32 Map::GetZoneId(uint16 areaflag,uint32 map_id)
{
    AreaTableEntry const *entry = GetAreaEntryByAreaFlagAndMap(areaflag,map_id);

    if (entry)
        return (entry->zone != 0) ? entry->zone : entry->ID;
    else
        return 0;
}

bool Map::IsInWater(float x, float y, float pZ, GridMapLiquidData *data) const
{
    // Check surface in x, y point for liquid
    if (const_cast<Map*>(this)->GetGrid(x, y))
    {
        GridMapLiquidData liquid_status;
        GridMapLiquidData *liquid_ptr = data ? data : &liquid_status;
        switch(getLiquidStatus(x, y, pZ, MAP_ALL_LIQUIDS, liquid_ptr))
        {
            case LIQUID_MAP_ABOVE_WATER:
            case LIQUID_MAP_NO_WATER:
            case LIQUID_MAP_WATER_WALK:
                return false;
            default:
                return true;
        }
    }
    return false;
}

bool Map::IsUnderWater(float x, float y, float z) const
{
    if (const_cast<Map*>(this)->GetGrid(x, y))
    {
        if (getLiquidStatus(x, y, z, MAP_LIQUID_TYPE_WATER | MAP_LIQUID_TYPE_OCEAN) == LIQUID_MAP_UNDER_WATER)
            return true;
    }
    return false;
}

bool Map::CheckGridIntegrity(Creature* c, bool moved) const
{
    Cell const& cur_cell = c->GetCurrentCell();

    CellPair xy_val = Trinity::ComputeCellPair(c->GetPositionX(), c->GetPositionY());
    Cell xy_cell(xy_val);
    if (xy_cell != cur_cell)
    {
        sLog.outDebug("Creature (GUIDLow: %u) X: %f Y: %f (%s) in grid[%u,%u]cell[%u,%u] instead grid[%u,%u]cell[%u,%u]",
            c->GetGUIDLow(),
            c->GetPositionX(),c->GetPositionY(),(moved ? "final" : "original"),
            cur_cell.GridX(), cur_cell.GridY(), cur_cell.CellX(), cur_cell.CellY(),
            xy_cell.GridX(),  xy_cell.GridY(),  xy_cell.CellX(),  xy_cell.CellY());
        return true;                                        // not crash at error, just output error in debug mode
    }

    return true;
}

const char* Map::GetMapName() const
{
    return i_mapEntry ? i_mapEntry->name[sWorld.GetDefaultDbcLocale()] : "UNNAMEDMAP\x0";
}

void Map::UpdateObjectVisibility(WorldObject* obj, Cell cell, CellPair cellpair)
{
    cell.SetNoCreate();
    Trinity::VisibleChangesNotifier notifier(*obj);
    TypeContainerVisitor<Trinity::VisibleChangesNotifier, WorldTypeMapContainer > player_notifier(notifier);
    cell.Visit(cellpair, player_notifier, *this, *obj, GetVisibilityDistance(obj));
}

void Map::UpdateObjectsVisibilityFor(Player* player, Cell cell, CellPair cellpair)
{
    Trinity::VisibleNotifier notifier(*player);

    cell.SetNoCreate();
    TypeContainerVisitor<Trinity::VisibleNotifier, WorldTypeMapContainer > world_notifier(notifier);
    TypeContainerVisitor<Trinity::VisibleNotifier, GridTypeMapContainer  > grid_notifier(notifier);
    cell.Visit(cellpair, world_notifier, *this, *player, GetVisibilityDistance());
    cell.Visit(cellpair, grid_notifier,  *this, *player, GetVisibilityDistance());

    // send data
    notifier.SendToSelf();
}

void Map::SendInitSelf(Player * player)
{
    sLog.outDetail("Creating player data for himself %u", player->GetGUIDLow());

    UpdateData data;

    bool hasTransport = false;

    // attach to player data current transport data
    if (Transport* transport = player->GetTransport())
    {
        hasTransport = true;
        transport->BuildCreateUpdateBlockForPlayer(&data, player);
    }

    // build data for self presence in world at own client (one time for map)
    player->BuildCreateUpdateBlockForPlayer(&data, player);

    // build other passengers at transport also (they always visible and marked as visible and will not send at visibility update at add to map
    if (Transport* transport = player->GetTransport())
    {
        for (Transport::PlayerSet::const_iterator itr = transport->GetPassengers().begin();itr!=transport->GetPassengers().end();++itr)
        {
            if (player!=(*itr) && player->HaveAtClient(*itr))
            {
                hasTransport = true;
                (*itr)->BuildCreateUpdateBlockForPlayer(&data, player);
            }
        }
    }

    WorldPacket packet;
    data.BuildPacket(&packet, hasTransport);
    player->GetSession()->SendPacket(&packet);
}

void Map::SendInitTransports(Player * player)
{
    // Hack to send out transports
    MapManager::TransportMap& tmap = sMapMgr.m_TransportsByMap;

    // no transports at map
    if (tmap.find(player->GetMapId()) == tmap.end())
        return;

    UpdateData transData;

    MapManager::TransportSet& tset = tmap[player->GetMapId()];

    bool hasTransport = false;

    for (MapManager::TransportSet::iterator i = tset.begin(); i != tset.end(); ++i)
    {
        // send data for current transport in other place
        if ((*i) != player->GetTransport()  && (*i)->GetMapId()==GetId())
        {
            hasTransport = true;
            (*i)->BuildCreateUpdateBlockForPlayer(&transData, player);
        }
    }

    WorldPacket packet;
    transData.BuildPacket(&packet, hasTransport);
    player->GetSession()->SendPacket(&packet);
}

void Map::SendRemoveTransports(Player * player)
{
    // Hack to send out transports
    MapManager::TransportMap& tmap = sMapMgr.m_TransportsByMap;

    // no transports at map
    if (tmap.find(player->GetMapId()) == tmap.end())
        return;

    UpdateData transData;

    MapManager::TransportSet& tset = tmap[player->GetMapId()];

    // except used transport
    for (MapManager::TransportSet::iterator i = tset.begin(); i != tset.end(); ++i)
        if ((*i) != player->GetTransport() && (*i)->GetMapId()!=GetId())
            (*i)->BuildOutOfRangeUpdateBlock(&transData);

    WorldPacket packet;
    transData.BuildPacket(&packet);
    player->GetSession()->SendPacket(&packet);
}

inline void Map::setNGrid(NGridType *grid, uint32 x, uint32 y)
{
    if (x >= MAX_NUMBER_OF_GRIDS || y >= MAX_NUMBER_OF_GRIDS)
    {
        sLog.outError("map::setNGrid() Invalid grid coordinates found: %d, %d!",x,y);
        assert(false);
    }
    i_grids[x][y] = grid;
}

void Map::DelayedUpdate(const uint32 t_diff)
{
    RemoveAllObjectsInRemoveList();

    // Don't unload grids if it's battleground, since we may have manually added GOs,creatures, those doesn't load from DB at grid re-load !
    // This isn't really bother us, since as soon as we have instanced BG-s, the whole map unloads as the BG gets ended
    if (!IsBattleGroundOrArena())
    {
        for (GridRefManager<NGridType>::iterator i = GridRefManager<NGridType>::begin(); i != GridRefManager<NGridType>::end();)
        {
            NGridType *grid = i->getSource();
            GridInfo *info = i->getSource()->getGridInfoRef();
            ++i;                                                // The update might delete the map and we need the next map before the iterator gets invalid
            assert(grid->GetGridState() >= 0 && grid->GetGridState() < MAX_GRID_STATE);
            si_GridStates[grid->GetGridState()]->Update(*this, *grid, *info, grid->getX(), grid->getY(), t_diff);
        }
    }
}

void Map::AddObjectToRemoveList(WorldObject *obj)
{
    assert(obj->GetMapId()==GetId() && obj->GetInstanceId()==GetInstanceId());

    obj->CleanupsBeforeDelete();                    // remove or simplify at least cross referenced links

    i_objectsToRemove.insert(obj);
    //sLog.outDebug("Object (GUID: %u TypeId: %u) added to removing list.",obj->GetGUIDLow(),obj->GetTypeId());
}

void Map::AddObjectToSwitchList(WorldObject *obj, bool on)
{
    assert(obj->GetMapId()==GetId() && obj->GetInstanceId()==GetInstanceId());

    std::map<WorldObject*, bool>::iterator itr = i_objectsToSwitch.find(obj);
    if (itr == i_objectsToSwitch.end())
        i_objectsToSwitch.insert(itr, std::make_pair(obj, on));
    else if (itr->second != on)
        i_objectsToSwitch.erase(itr);
    else
        assert(false);
}

void Map::RemoveAllObjectsInRemoveList()
{
    while (!i_objectsToSwitch.empty())
    {
        std::map<WorldObject*, bool>::iterator itr = i_objectsToSwitch.begin();
        WorldObject *obj = itr->first;
        bool on = itr->second;
        i_objectsToSwitch.erase(itr);

        switch (obj->GetTypeId())
        {
        case TYPEID_UNIT:
            if (!((Creature*)obj)->isPet())
                SwitchGridContainers((Creature*)obj, on);
            break;
        }
    }

    //sLog.outDebug("Object remover 1 check.");
    while (!i_objectsToRemove.empty())
    {
        std::set<WorldObject*>::iterator itr = i_objectsToRemove.begin();
        WorldObject* obj = *itr;

        switch (obj->GetTypeId())
        {
            case TYPEID_CORPSE:
            {
                Corpse* corpse = ObjectAccessor::Instance().GetCorpse(obj->GetGUID());
                if (!corpse)
                    sLog.outError("Try delete corpse/bones %u that not in map", obj->GetGUIDLow());
                else
                    Remove(corpse,true);
                break;
            }
        case TYPEID_DYNAMICOBJECT:
            Remove((DynamicObject*)obj,true);
            break;
        case TYPEID_GAMEOBJECT:
            Remove((GameObject*)obj,true);
            break;
        case TYPEID_UNIT:
            // in case triggered sequence some spell can continue casting after prev CleanupsBeforeDelete call
            // make sure that like sources auras/etc removed before destructor start
            Remove((Creature*)obj,true);
            break;
        default:
            sLog.outError("Non-grid object (TypeId: %u) in grid object removing list, ignored.",obj->GetTypeId());
            break;
        }

        i_objectsToRemove.erase(itr);
    }
    //sLog.outDebug("Object remover 2 check.");
}

uint32 Map::GetPlayersCountExceptGMs() const
{
    uint32 count = 0;
    for (MapRefManager::const_iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
        if (!itr->getSource()->isGameMaster())
            ++count;
    return count;
}

void Map::SendToPlayers(WorldPacket const* data) const
{
    for (MapRefManager::const_iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
        itr->getSource()->GetSession()->SendPacket(data);
}

bool Map::ActiveObjectsNearGrid(uint32 x, uint32 y) const
{
    CellPair cell_min(x*MAX_NUMBER_OF_CELLS, y*MAX_NUMBER_OF_CELLS);
    CellPair cell_max(cell_min.x_coord + MAX_NUMBER_OF_CELLS, cell_min.y_coord+MAX_NUMBER_OF_CELLS);

    //we must find visible range in cells so we unload only non-visible cells...
    float viewDist = GetVisibilityDistance();
    int cell_range = (int)ceilf(viewDist / SIZE_OF_GRID_CELL) + 1;

    cell_min << cell_range;
    cell_min -= cell_range;
    cell_max >> cell_range;
    cell_max += cell_range;

    for (MapRefManager::const_iterator iter = m_mapRefManager.begin(); iter != m_mapRefManager.end(); ++iter)
    {
        Player* plr = iter->getSource();

        CellPair p = Trinity::ComputeCellPair(plr->GetPositionX(), plr->GetPositionY());
        if ((cell_min.x_coord <= p.x_coord && p.x_coord <= cell_max.x_coord) &&
            (cell_min.y_coord <= p.y_coord && p.y_coord <= cell_max.y_coord))
            return true;
    }

    for (ActiveNonPlayers::const_iterator iter = m_activeNonPlayers.begin(); iter != m_activeNonPlayers.end(); ++iter)
    {
        WorldObject* obj = *iter;

        CellPair p = Trinity::ComputeCellPair(obj->GetPositionX(), obj->GetPositionY());
        if ((cell_min.x_coord <= p.x_coord && p.x_coord <= cell_max.x_coord) &&
            (cell_min.y_coord <= p.y_coord && p.y_coord <= cell_max.y_coord))
            return true;
    }

    return false;
}

void Map::AddToActive(Creature* c)
{
    AddToActiveHelper(c);

    // also not allow unloading spawn grid to prevent creating creature clone at load
    if (!c->isPet() && c->GetDBTableGUIDLow())
    {
        float x,y,z;
        c->GetRespawnCoord(x,y,z);
        GridPair p = Trinity::ComputeGridPair(x, y);
        if (getNGrid(p.x_coord, p.y_coord))
            getNGrid(p.x_coord, p.y_coord)->incUnloadActiveLock();
        else
        {
            GridPair p2 = Trinity::ComputeGridPair(c->GetPositionX(), c->GetPositionY());
            sLog.outError("Active creature (GUID: %u Entry: %u) added to grid[%u,%u] but spawn grid[%u,%u] not loaded.",
                c->GetGUIDLow(), c->GetEntry(), p.x_coord, p.y_coord, p2.x_coord, p2.y_coord);
        }
    }
}

void Map::RemoveFromActive(Creature* c)
{
    RemoveFromActiveHelper(c);

    // also allow unloading spawn grid
    if (!c->isPet() && c->GetDBTableGUIDLow())
    {
        float x,y,z;
        c->GetRespawnCoord(x,y,z);
        GridPair p = Trinity::ComputeGridPair(x, y);
        if (getNGrid(p.x_coord, p.y_coord))
            getNGrid(p.x_coord, p.y_coord)->decUnloadActiveLock();
        else
        {
            GridPair p2 = Trinity::ComputeGridPair(c->GetPositionX(), c->GetPositionY());
            sLog.outError("Active creature (GUID: %u Entry: %u) removed from grid[%u,%u] but spawn grid[%u,%u] not loaded.",
                c->GetGUIDLow(), c->GetEntry(), p.x_coord, p.y_coord, p2.x_coord, p2.y_coord);
        }
    }
}
void Map::ScriptsStart(ScriptMapMap const& scripts, uint32 id, Object* source, Object* target)
{
    ///- Find the script map
    ScriptMapMap::const_iterator s = scripts.find(id);
    if (s == scripts.end())
        return;

    // prepare static data
    uint64 sourceGUID = source ? source->GetGUID() : (uint64)0; //some script commands doesn't have source
    uint64 targetGUID = target ? target->GetGUID() : (uint64)0;
    uint64 ownerGUID  = (source->GetTypeId()==TYPEID_ITEM) ? ((Item*)source)->GetOwnerGUID() : (uint64)0;

    ///- Schedule script execution for all scripts in the script map
    ScriptMap const *s2 = &(s->second);
    bool immedScript = false;
    for (ScriptMap::const_iterator iter = s2->begin(); iter != s2->end(); ++iter)
    {
        ScriptAction sa;
        sa.sourceGUID = sourceGUID;
        sa.targetGUID = targetGUID;
        sa.ownerGUID  = ownerGUID;

        sa.script = &iter->second;
        m_scriptSchedule.insert(std::pair<time_t, ScriptAction>(time_t(sWorld.GetGameTime() + iter->first), sa));
        if (iter->first == 0)
            immedScript = true;

        sWorld.IncreaseScheduledScriptsCount();
    }
    ///- If one of the effects should be immediate, launch the script execution
    if (/*start &&*/ immedScript && !i_scriptLock)
    {
        i_scriptLock = true;
        ScriptsProcess();
        i_scriptLock = false;
    }
}

void Map::ScriptCommandStart(ScriptInfo const& script, uint32 delay, Object* source, Object* target)
{
    // NOTE: script record _must_ exist until command executed

    // prepare static data
    uint64 sourceGUID = source ? source->GetGUID() : (uint64)0;
    uint64 targetGUID = target ? target->GetGUID() : (uint64)0;
    uint64 ownerGUID  = (source->GetTypeId()==TYPEID_ITEM) ? ((Item*)source)->GetOwnerGUID() : (uint64)0;

    ScriptAction sa;
    sa.sourceGUID = sourceGUID;
    sa.targetGUID = targetGUID;
    sa.ownerGUID  = ownerGUID;

    sa.script = &script;
    m_scriptSchedule.insert(std::pair<time_t, ScriptAction>(time_t(sWorld.GetGameTime() + delay), sa));

    sWorld.IncreaseScheduledScriptsCount();

    ///- If effects should be immediate, launch the script execution
    if (delay == 0 && !i_scriptLock)
    {
        i_scriptLock = true;
        ScriptsProcess();
        i_scriptLock = false;
    }
}

/// Process queued scripts
void Map::ScriptsProcess()
{
    if (m_scriptSchedule.empty())
        return;

    ///- Process overdue queued scripts
    std::multimap<time_t, ScriptAction>::iterator iter = m_scriptSchedule.begin();
                                                            // ok as multimap is a *sorted* associative container
    while (!m_scriptSchedule.empty() && (iter->first <= sWorld.GetGameTime()))
    {
        ScriptAction const& step = iter->second;

        Object* source = NULL;

        if (step.sourceGUID)
        {
            switch (GUID_HIPART(step.sourceGUID))
            {
                case HIGHGUID_ITEM:
                    // case HIGHGUID_CONTAINER: ==HIGHGUID_ITEM
                    {
                        Player* player = HashMapHolder<Player>::Find(step.ownerGUID);
                        if (player)
                            source = player->GetItemByGuid(step.sourceGUID);
                        break;
                    }
                case HIGHGUID_UNIT:
                    source = GetCreature(step.sourceGUID);
                    break;
                case HIGHGUID_PET:
                    source = HashMapHolder<Pet>::Find(step.sourceGUID);
                    break;
                case HIGHGUID_PLAYER:
                    source = HashMapHolder<Player>::Find(step.sourceGUID);
                    break;
                case HIGHGUID_GAMEOBJECT:
                    source = GetGameObject(step.sourceGUID);
                    break;
                case HIGHGUID_CORPSE:
                    source = HashMapHolder<Corpse>::Find(step.sourceGUID);
                    break;
                case HIGHGUID_MO_TRANSPORT:
                    for (MapManager::TransportSet::iterator iter = sMapMgr.m_Transports.begin(); iter != sMapMgr.m_Transports.end(); ++iter)
                    {
                        if ((*iter)->GetGUID() == step.sourceGUID)
                        {
                            source = reinterpret_cast<Object*>(*iter);
                            break;
                        }
                    }
                    break;
                default:
                    sLog.outError("*_script source with unsupported high guid value %u",GUID_HIPART(step.sourceGUID));
                    break;
            }
        }

        //if(source && !source->IsInWorld()) source = NULL;

        Object* target = NULL;

        if (step.targetGUID)
        {
            switch (GUID_HIPART(step.targetGUID))
            {
                case HIGHGUID_UNIT:
                    target = GetCreature(step.targetGUID);
                    break;
                case HIGHGUID_PET:
                    target = HashMapHolder<Pet>::Find(step.targetGUID);
                    break;
                case HIGHGUID_PLAYER:                       // empty GUID case also
                    target = HashMapHolder<Player>::Find(step.targetGUID);
                    break;
                case HIGHGUID_GAMEOBJECT:
                    target = GetGameObject(step.targetGUID);
                    break;
                case HIGHGUID_CORPSE:
                    target = HashMapHolder<Corpse>::Find(step.targetGUID);
                    break;
                default:
                    sLog.outError("*_script source with unsupported high guid value %u",GUID_HIPART(step.targetGUID));
                    break;
            }
        }

        //if(target && !target->IsInWorld()) target = NULL;

        switch (step.script->command)
        {
            case SCRIPT_COMMAND_TALK:
            {
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_TALK call for NULL creature.");
                    break;
                }

                if (source->GetTypeId()!=TYPEID_UNIT)
                {
                    sLog.outError("SCRIPT_COMMAND_TALK call for non-creature (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }
                if (step.script->datalong > 3)
                {
                    sLog.outError("SCRIPT_COMMAND_TALK invalid chat type (%u), skipping.",step.script->datalong);
                    break;
                }

                uint64 unit_target = target ? target->GetGUID() : 0;

                //datalong 0=normal say, 1=whisper, 2=yell, 3=emote text
                switch (step.script->datalong)
                {
                    case 0:                                 // Say
                        ((Creature *)source)->Say(step.script->dataint, LANG_UNIVERSAL, unit_target);
                        break;
                    case 1:                                 // Whisper
                        if (!unit_target)
                        {
                            sLog.outError("SCRIPT_COMMAND_TALK attempt to whisper (%u) NULL, skipping.",step.script->datalong);
                            break;
                        }
                        ((Creature *)source)->Whisper(step.script->dataint,unit_target);
                        break;
                    case 2:                                 // Yell
                        ((Creature *)source)->Yell(step.script->dataint, LANG_UNIVERSAL, unit_target);
                        break;
                    case 3:                                 // Emote text
                        ((Creature *)source)->TextEmote(step.script->dataint, unit_target);
                        break;
                    default:
                        break;                              // must be already checked at load
                }
                break;
            }

            case SCRIPT_COMMAND_EMOTE:
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_EMOTE call for NULL creature.");
                    break;
                }

                if (source->GetTypeId()!=TYPEID_UNIT)
                {
                    sLog.outError("SCRIPT_COMMAND_EMOTE call for non-creature (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                ((Creature *)source)->HandleEmoteCommand(step.script->datalong);
                break;
            case SCRIPT_COMMAND_FIELD_SET:
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_FIELD_SET call for NULL object.");
                    break;
                }
                if (step.script->datalong <= OBJECT_FIELD_ENTRY || step.script->datalong >= source->GetValuesCount())
                {
                    sLog.outError("SCRIPT_COMMAND_FIELD_SET call for wrong field %u (max count: %u) in object (TypeId: %u).",
                        step.script->datalong,source->GetValuesCount(),source->GetTypeId());
                    break;
                }

                source->SetUInt32Value(step.script->datalong, step.script->datalong2);
                break;
            case SCRIPT_COMMAND_MOVE_TO:
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_MOVE_TO call for NULL creature.");
                    break;
                }

                if (source->GetTypeId()!=TYPEID_UNIT)
                {
                    sLog.outError("SCRIPT_COMMAND_MOVE_TO call for non-creature (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }
                ((Unit *)source)->SendMonsterMoveWithSpeed(step.script->x, step.script->y, step.script->z, step.script->datalong2);
                ((Unit *)source)->GetMap()->CreatureRelocation(((Creature *)source), step.script->x, step.script->y, step.script->z, 0);
                break;
            case SCRIPT_COMMAND_FLAG_SET:
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_FLAG_SET call for NULL object.");
                    break;
                }
                if (step.script->datalong <= OBJECT_FIELD_ENTRY || step.script->datalong >= source->GetValuesCount())
                {
                    sLog.outError("SCRIPT_COMMAND_FLAG_SET call for wrong field %u (max count: %u) in object (TypeId: %u).",
                        step.script->datalong,source->GetValuesCount(),source->GetTypeId());
                    break;
                }

                source->SetFlag(step.script->datalong, step.script->datalong2);
                break;
            case SCRIPT_COMMAND_FLAG_REMOVE:
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_FLAG_REMOVE call for NULL object.");
                    break;
                }
                if (step.script->datalong <= OBJECT_FIELD_ENTRY || step.script->datalong >= source->GetValuesCount())
                {
                    sLog.outError("SCRIPT_COMMAND_FLAG_REMOVE call for wrong field %u (max count: %u) in object (TypeId: %u).",
                        step.script->datalong,source->GetValuesCount(),source->GetTypeId());
                    break;
                }

                source->RemoveFlag(step.script->datalong, step.script->datalong2);
                break;

            case SCRIPT_COMMAND_TELEPORT_TO:
            {
                // accept player in any one from target/source arg
                if (!target && !source)
                {
                    sLog.outError("SCRIPT_COMMAND_TELEPORT_TO call for NULL object.");
                    break;
                }

                                                            // must be only Player
                if ((!target || target->GetTypeId() != TYPEID_PLAYER) && (!source || source->GetTypeId() != TYPEID_PLAYER))
                {
                    sLog.outError("SCRIPT_COMMAND_TELEPORT_TO call for non-player (TypeIdSource: %u)(TypeIdTarget: %u), skipping.", source ? source->GetTypeId() : 0, target ? target->GetTypeId() : 0);
                    break;
                }

                Player* pSource = target && target->GetTypeId() == TYPEID_PLAYER ? (Player*)target : (Player*)source;

                pSource->TeleportTo(step.script->datalong, step.script->x, step.script->y, step.script->z, step.script->o);
                break;
            }

            case SCRIPT_COMMAND_TEMP_SUMMON_CREATURE:
            {
                if (!step.script->datalong)                  // creature not specified
                {
                    sLog.outError("SCRIPT_COMMAND_TEMP_SUMMON_CREATURE call for NULL creature.");
                    break;
                }

                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_TEMP_SUMMON_CREATURE call for NULL world object.");
                    break;
                }

                WorldObject* summoner = dynamic_cast<WorldObject*>(source);

                if (!summoner)
                {
                    sLog.outError("SCRIPT_COMMAND_TEMP_SUMMON_CREATURE call for non-WorldObject (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                float x = step.script->x;
                float y = step.script->y;
                float z = step.script->z;
                float o = step.script->o;

                Creature* pCreature = summoner->SummonCreature(step.script->datalong, x, y, z, o,TEMPSUMMON_TIMED_OR_DEAD_DESPAWN,step.script->datalong2);
                if (!pCreature)
                {
                    sLog.outError("SCRIPT_COMMAND_TEMP_SUMMON failed for creature (entry: %u).",step.script->datalong);
                    break;
                }

                break;
            }

            case SCRIPT_COMMAND_RESPAWN_GAMEOBJECT:
            {
                if (!step.script->datalong)                  // gameobject not specified
                {
                    sLog.outError("SCRIPT_COMMAND_RESPAWN_GAMEOBJECT call for NULL gameobject.");
                    break;
                }

                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_RESPAWN_GAMEOBJECT call for NULL world object.");
                    break;
                }

                WorldObject* summoner = dynamic_cast<WorldObject*>(source);

                if (!summoner)
                {
                    sLog.outError("SCRIPT_COMMAND_RESPAWN_GAMEOBJECT call for non-WorldObject (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                GameObject *go = NULL;
                int32 time_to_despawn = step.script->datalong2<5 ? 5 : (int32)step.script->datalong2;

                Trinity::GameObjectWithDbGUIDCheck go_check(*summoner,step.script->datalong);
                Trinity::GameObjectSearcher<Trinity::GameObjectWithDbGUIDCheck> checker(go,go_check);

                Cell::VisitGridObjects(summoner, checker, GetVisibilityDistance());

                if (!go)
                {
                    sLog.outError("SCRIPT_COMMAND_RESPAWN_GAMEOBJECT failed for gameobject(guid: %u).", step.script->datalong);
                    break;
                }

                if (go->GetGoType()==GAMEOBJECT_TYPE_FISHINGNODE ||
                    go->GetGoType()==GAMEOBJECT_TYPE_FISHINGNODE ||
                    go->GetGoType()==GAMEOBJECT_TYPE_DOOR        ||
                    go->GetGoType()==GAMEOBJECT_TYPE_BUTTON      ||
                    go->GetGoType()==GAMEOBJECT_TYPE_TRAP)
                {
                    sLog.outError("SCRIPT_COMMAND_RESPAWN_GAMEOBJECT can not be used with gameobject of type %u (guid: %u).", uint32(go->GetGoType()), step.script->datalong);
                    break;
                }

                if (go->isSpawned())
                    break;                                  //gameobject already spawned

                go->SetLootState(GO_READY);
                go->SetRespawnTime(time_to_despawn);        //despawn object in ? seconds

                go->GetMap()->Add(go);
                break;
            }
            case SCRIPT_COMMAND_OPEN_DOOR:
            {
                if (!step.script->datalong)                  // door not specified
                {
                    sLog.outError("SCRIPT_COMMAND_OPEN_DOOR call for NULL door.");
                    break;
                }

                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_OPEN_DOOR call for NULL unit.");
                    break;
                }

                if (!source->isType(TYPEMASK_UNIT))          // must be any Unit (creature or player)
                {
                    sLog.outError("SCRIPT_COMMAND_OPEN_DOOR call for non-unit (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                Unit* caster = (Unit*)source;

                GameObject *door = NULL;
                int32 time_to_close = step.script->datalong2 < 15 ? 15 : (int32)step.script->datalong2;

                Trinity::GameObjectWithDbGUIDCheck go_check(*caster,step.script->datalong);
                Trinity::GameObjectSearcher<Trinity::GameObjectWithDbGUIDCheck> checker(door,go_check);

                Cell::VisitGridObjects(caster, checker, GetVisibilityDistance());

                if (!door)
                {
                    sLog.outError("SCRIPT_COMMAND_OPEN_DOOR failed for gameobject(guid: %u).", step.script->datalong);
                    break;
                }
                if (door->GetGoType() != GAMEOBJECT_TYPE_DOOR)
                {
                    sLog.outError("SCRIPT_COMMAND_OPEN_DOOR failed for non-door(GoType: %u).", door->GetGoType());
                    break;
                }

                if (door->GetGoState() != GO_STATE_READY)
                    break;                                  //door already  open

                door->UseDoorOrButton(time_to_close);

                if (target && target->isType(TYPEMASK_GAMEOBJECT) && ((GameObject*)target)->GetGoType()==GAMEOBJECT_TYPE_BUTTON)
                    ((GameObject*)target)->UseDoorOrButton(time_to_close);
                break;
            }
            case SCRIPT_COMMAND_CLOSE_DOOR:
            {
                if (!step.script->datalong)                  // guid for door not specified
                {
                    sLog.outError("SCRIPT_COMMAND_CLOSE_DOOR call for NULL door.");
                    break;
                }

                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_CLOSE_DOOR call for NULL unit.");
                    break;
                }

                if (!source->isType(TYPEMASK_UNIT))          // must be any Unit (creature or player)
                {
                    sLog.outError("SCRIPT_COMMAND_CLOSE_DOOR call for non-unit (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                Unit* caster = (Unit*)source;

                GameObject *door = NULL;
                int32 time_to_open = step.script->datalong2 < 15 ? 15 : (int32)step.script->datalong2;

                Trinity::GameObjectWithDbGUIDCheck go_check(*caster,step.script->datalong);
                Trinity::GameObjectSearcher<Trinity::GameObjectWithDbGUIDCheck> checker(door,go_check);

                Cell::VisitGridObjects(caster, checker, GetVisibilityDistance());

                if (!door)
                {
                    sLog.outError("SCRIPT_COMMAND_CLOSE_DOOR failed for gameobject(guid: %u).", step.script->datalong);
                    break;
                }

                if (door->GetGoType() != GAMEOBJECT_TYPE_DOOR)
                {
                    sLog.outError("SCRIPT_COMMAND_CLOSE_DOOR failed for non-door(GoType: %u).", door->GetGoType());
                    break;
                }

                if (door->GetGoState() == GO_STATE_READY)
                    break;                                  //door already closed

                door->UseDoorOrButton(time_to_open);

                if (target && target->isType(TYPEMASK_GAMEOBJECT) && ((GameObject*)target)->GetGoType()==GAMEOBJECT_TYPE_BUTTON)
                    ((GameObject*)target)->UseDoorOrButton(time_to_open);

                break;
            }
            case SCRIPT_COMMAND_QUEST_EXPLORED:
            {
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_QUEST_EXPLORED call for NULL source.");
                    break;
                }

                if (!target)
                {
                    sLog.outError("SCRIPT_COMMAND_QUEST_EXPLORED call for NULL target.");
                    break;
                }

                // when script called for item spell casting then target == (unit or GO) and source is player
                WorldObject* worldObject;
                Player* player;

                if (target->GetTypeId()==TYPEID_PLAYER)
                {
                    if (source->GetTypeId()!=TYPEID_UNIT && source->GetTypeId()!=TYPEID_GAMEOBJECT)
                    {
                        sLog.outError("SCRIPT_COMMAND_QUEST_EXPLORED call for non-creature and non-gameobject (TypeId: %u), skipping.",source->GetTypeId());
                        break;
                    }

                    worldObject = (WorldObject*)source;
                    player = (Player*)target;
                }
                else
                {
                    if (target->GetTypeId()!=TYPEID_UNIT && target->GetTypeId()!=TYPEID_GAMEOBJECT)
                    {
                        sLog.outError("SCRIPT_COMMAND_QUEST_EXPLORED call for non-creature and non-gameobject (TypeId: %u), skipping.",target->GetTypeId());
                        break;
                    }

                    if (source->GetTypeId()!=TYPEID_PLAYER)
                    {
                        sLog.outError("SCRIPT_COMMAND_QUEST_EXPLORED call for non-player(TypeId: %u), skipping.",source->GetTypeId());
                        break;
                    }

                    worldObject = (WorldObject*)target;
                    player = (Player*)source;
                }

                // quest id and flags checked at script loading
                if ((worldObject->GetTypeId()!=TYPEID_UNIT || ((Unit*)worldObject)->isAlive()) &&
                    (step.script->datalong2==0 || worldObject->IsWithinDistInMap(player,float(step.script->datalong2))))
                    player->AreaExploredOrEventHappens(step.script->datalong);
                else
                    player->FailQuest(step.script->datalong);

                break;
            }

            case SCRIPT_COMMAND_ACTIVATE_OBJECT:
            {
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_ACTIVATE_OBJECT must have source caster.");
                    break;
                }

                if (!source->isType(TYPEMASK_UNIT))
                {
                    sLog.outError("SCRIPT_COMMAND_ACTIVATE_OBJECT source caster isn't unit (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                if (!target)
                {
                    sLog.outError("SCRIPT_COMMAND_ACTIVATE_OBJECT call for NULL gameobject.");
                    break;
                }

                if (target->GetTypeId()!=TYPEID_GAMEOBJECT)
                {
                    sLog.outError("SCRIPT_COMMAND_ACTIVATE_OBJECT call for non-gameobject (TypeId: %u), skipping.",target->GetTypeId());
                    break;
                }

                Unit* caster = (Unit*)source;

                GameObject *go = (GameObject*)target;

                go->Use(caster);
                break;
            }

            case SCRIPT_COMMAND_REMOVE_AURA:
            {
                Object* cmdTarget = step.script->datalong2 ? source : target;

                if (!cmdTarget)
                {
                    sLog.outError("SCRIPT_COMMAND_REMOVE_AURA call for NULL %s.",step.script->datalong2 ? "source" : "target");
                    break;
                }

                if (!cmdTarget->isType(TYPEMASK_UNIT))
                {
                    sLog.outError("SCRIPT_COMMAND_REMOVE_AURA %s isn't unit (TypeId: %u), skipping.",step.script->datalong2 ? "source" : "target",cmdTarget->GetTypeId());
                    break;
                }

                ((Unit*)cmdTarget)->RemoveAurasDueToSpell(step.script->datalong);
                break;
            }

            case SCRIPT_COMMAND_CAST_SPELL:
            {
                if (!source)
                {
                    sLog.outDebug("SCRIPT_COMMAND_CAST_SPELL must have source caster.");
                    break;
                }

                if (!source->isType(TYPEMASK_UNIT))
                {
                    sLog.outDebug("SCRIPT_COMMAND_CAST_SPELL source caster isn't unit (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                Object* cmdTarget = step.script->datalong2 ? source : target;

                if (!cmdTarget)
                {
                    sLog.outDebug("SCRIPT_COMMAND_CAST_SPELL call for NULL %s.",step.script->datalong2 ? "source" : "target");
                    break;
                }

                if (!cmdTarget->isType(TYPEMASK_UNIT))
                {
                    sLog.outDebug("SCRIPT_COMMAND_CAST_SPELL %s isn't unit (TypeId: %u), skipping.",step.script->datalong2 ? "source" : "target",cmdTarget->GetTypeId());
                    break;
                }

                Unit* spellTarget = (Unit*)cmdTarget;

                //TODO: when GO cast implemented, code below must be updated accordingly to also allow GO spell cast
                ((Unit*)source)->CastSpell(spellTarget,step.script->datalong,false);

                break;
            }

            case SCRIPT_COMMAND_LOAD_PATH:
            {
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_START_MOVE is tried to apply to NON-existing unit.");
                    break;
                }

                if (!source->isType(TYPEMASK_UNIT))
                {
                    sLog.outError("SCRIPT_COMMAND_START_MOVE source mover isn't unit (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                if (!WaypointMgr.GetPath(step.script->datalong))
                {
                    sLog.outError("SCRIPT_COMMAND_START_MOVE source mover has an invallid path, skipping.", step.script->datalong2);
                    break;
                }

                dynamic_cast<Unit*>(source)->GetMotionMaster()->MovePath(step.script->datalong, step.script->datalong2);
                break;
            }

            case SCRIPT_COMMAND_CALLSCRIPT_TO_UNIT:
            {
                if (!step.script->datalong || !step.script->datalong2)
                {
                    sLog.outError("SCRIPT_COMMAND_CALLSCRIPT calls invallid db_script_id or lowguid not present: skipping.");
                    break;
                }
                //our target
                Creature* target = NULL;

                if (source) //using grid searcher
                {
                    //sLog.outDebug("Attempting to find Creature: Db GUID: %i", step.script->datalong);
                    Trinity::CreatureWithDbGUIDCheck target_check(((Unit*)source), step.script->datalong);
                    Trinity::CreatureSearcher<Trinity::CreatureWithDbGUIDCheck> checker(target,target_check);

                    Cell::VisitGridObjects((Unit*)source, checker, GetVisibilityDistance());
                }
                else //check hashmap holders
                {
                    if (CreatureData const* data = objmgr.GetCreatureData(step.script->datalong))
                        target = GetCreature(MAKE_NEW_GUID(step.script->datalong, data->id, HIGHGUID_UNIT), data->posX, data->posY);
                }
                //sLog.outDebug("attempting to pass target...");
                if (!target)
                    break;
                //sLog.outDebug("target passed");
                //Lets choose our ScriptMap map
                ScriptMapMap *datamap = NULL;
                switch (step.script->dataint)
                {
                    case 1://QUEST END SCRIPTMAP
                        datamap = &sQuestEndScripts;
                        break;
                    case 2://QUEST START SCRIPTMAP
                        datamap = &sQuestStartScripts;
                        break;
                    case 3://SPELLS SCRIPTMAP
                        datamap = &sSpellScripts;
                        break;
                    case 4://GAMEOBJECTS SCRIPTMAP
                        datamap = &sGameObjectScripts;
                        break;
                    case 5://EVENTS SCRIPTMAP
                        datamap = &sEventScripts;
                        break;
                    case 6://WAYPOINTS SCRIPTMAP
                        datamap = &sWaypointScripts;
                        break;
                    default:
                        sLog.outError("SCRIPT_COMMAND_CALLSCRIPT ERROR: no scriptmap present... ignoring");
                        break;
                }
                //if no scriptmap present...
                if (!datamap)
                    break;

                uint32 script_id = step.script->datalong2;
                //insert script into schedule but do not start it
                ScriptsStart(*datamap, script_id, target, NULL/*, false*/);
                break;
            }

            case SCRIPT_COMMAND_PLAY_SOUND:
            {
                if (!source)
                {
                    sLog.outError("SCRIPT_COMMAND_PLAY_SOUND call for NULL creature.");
                    break;
                }

                WorldObject* pSource = dynamic_cast<WorldObject*>(source);
                if (!pSource)
                {
                    sLog.outError("SCRIPT_COMMAND_PLAY_SOUND call for non-world object (TypeId: %u), skipping.",source->GetTypeId());
                    break;
                }

                // bitmask: 0/1=anyone/target, 0/2=with distance dependent
                Player* pTarget = NULL;
                if (step.script->datalong2 & 1)
                {
                    if (!target)
                    {
                        sLog.outError("SCRIPT_COMMAND_PLAY_SOUND in targeted mode call for NULL target.");
                        break;
                    }

                    if (target->GetTypeId()!=TYPEID_PLAYER)
                    {
                        sLog.outError("SCRIPT_COMMAND_PLAY_SOUND in targeted mode call for non-player (TypeId: %u), skipping.",target->GetTypeId());
                        break;
                    }

                    pTarget = (Player*)target;
                }

                // bitmask: 0/1=anyone/target, 0/2=with distance dependent
                if (step.script->datalong2 & 2)
                    pSource->PlayDistanceSound(step.script->datalong, pTarget);
                else
                    pSource->PlayDirectSound(step.script->datalong, pTarget);
                break;
            }

            case SCRIPT_COMMAND_KILL:
            {
                if (!source || ((Creature*)source)->isDead())
                    break;

                switch (step.script->datalong)
                {
                    default: // backward compatibility (defaults to 0)
                    case 0: // source kills source
                        ((Creature*)source)->DealDamage(((Creature*)source), ((Creature*)source)->GetHealth(), DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
                        break;
                    case 1: // target kills source
                        ((Creature*)target)->DealDamage(((Creature*)source), ((Creature*)source)->GetHealth(), DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
                        break;
                    case 2: // source kills target
                        if (target)
                            ((Creature*)source)->DealDamage(((Creature*)target), ((Creature*)target)->GetHealth(), DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
                        break;
                }


                switch (step.script->dataint)
                {
                case 0: break; //return false not remove corpse
                case 1: ((Creature*)source)->RemoveCorpse(); break;
                }
                break;
            }

            default:
                sLog.outError("Unknown script command %u called.",step.script->command);
                break;
        }

        m_scriptSchedule.erase(iter);
        sWorld.DecreaseScheduledScriptCount();

        iter = m_scriptSchedule.begin();
    }
}


template void Map::Add(Corpse *);
template void Map::Add(Creature *);
template void Map::Add(GameObject *);
template void Map::Add(DynamicObject *);

template void Map::Remove(Corpse *,bool);
template void Map::Remove(Creature *,bool);
template void Map::Remove(GameObject *, bool);
template void Map::Remove(DynamicObject *, bool);

/* ******* Dungeon Instance Maps ******* */

InstanceMap::InstanceMap(uint32 id, time_t expiry, uint32 InstanceId, uint8 SpawnMode)
  : Map(id, expiry, InstanceId, SpawnMode), i_data(NULL),
    m_resetAfterUnload(false), m_unloadWhenEmpty(false), m_unlootedCreaturesSummoned(false)
{

    InstanceMap::InitVisibilityDistance();

    // the timer is started by default, and stopped when the first player joins
    // this make sure it gets unloaded if for some reason no player joins
    m_unloadTimer = std::max(sWorld.getConfig(CONFIG_INSTANCE_UNLOAD_DELAY), (uint32)MIN_UNLOAD_DELAY);

    m_VisibilityNotifyPeriod = World::GetVisibilityNotifyPeriodInInstances();
}

InstanceMap::~InstanceMap()
{
    if (i_data)
    {
        delete i_data;
        i_data = NULL;
    }
}

void InstanceMap::InitVisibilityDistance()
{
    //init visibility distance for instances
    switch (i_mapEntry->MapID)
    {
        case 550:   //The Eye
        case 534:   //Hyjal Summit
        case 564:   //Black Temple
            m_VisibleDistance = sWorld.GetMaxSpecialVisibleDistance();
            break;
        default:
            m_VisibleDistance = sWorld.GetMaxVisibleDistanceInInstances();
            break;
    }
}

/*
    Do map specific checks to see if the player can enter
*/
bool InstanceMap::EncounterInProgress(Player *player)
{
    if (!player->isGameMaster() && GetInstanceData() && GetInstanceData()->IsEncounterInProgress())
    {
        sLog.outDebug("InstanceMap: Player '%s' can't enter instance '%s' while an encounter is in progress.", player->GetName(),GetMapName());
        player->SendTransferAborted(GetId(),TRANSFER_ABORT_ZONE_IN_COMBAT);
        return true;
    }
    return false;
}

bool InstanceMap::CanEnter(Player *player)
{
    if (player->GetMapRef().getTarget() == this)
    {
        sLog.outError("InstanceMap::CanEnter - player %s(%u) already in map %d,%d,%d!", player->GetName(), player->GetGUIDLow(), GetId(), GetInstanceId(), GetSpawnMode());
        assert(false);
        return false;
    }

    // cannot enter if the instance is full (player cap), GMs don't count
    uint32 maxPlayers = GetMaxPlayers();
    if (!player->isGameMaster() && GetPlayersCountExceptGMs() >= maxPlayers)
    {
        sLog.outDetail("MAP: Instance '%u' of map '%s' cannot have more than '%u' players. Player '%s' rejected", GetInstanceId(), GetMapName(), maxPlayers, player->GetName());
        player->SendTransferAborted(GetId(), TRANSFER_ABORT_MAX_PLAYERS);
        return false;
    }

    if (EncounterInProgress(player))
        return false;

    return Map::CanEnter(player);
}

/*
    Do map specific checks and add the player to the map if successful.
*/
bool InstanceMap::Add(Player *player)
{
    // TODO: Not sure about checking player level: already done in HandleAreaTriggerOpcode
    // GMs still can teleport player in instance.
    // Is it needed?

    {
        Guard guard(*this);
        if (!CanEnter(player))
            return false;

        // Dungeon only code
        if(IsDungeon())
        {
            // get or create an instance save for the map
            InstanceSave *mapSave = sInstanceSaveManager.GetInstanceSave(GetInstanceId());
            if(!mapSave)
            {
                sLog.outDetail("InstanceMap::Add: creating instance save for map %d spawnmode %d with instance id %d", GetId(), GetSpawnMode(), GetInstanceId());
                mapSave = sInstanceSaveManager.AddInstanceSave(GetId(), GetInstanceId(), GetSpawnMode(), 0, true);
            }
            // check for existing instance binds
            InstancePlayerBind *playerBind = player->GetBoundInstance(GetId(), GetSpawnMode());
            if(playerBind && playerBind->perm)
            {
                // cannot enter other instances if bound permanently
                if(playerBind->save != mapSave)
                {
                    sLog.outError("InstanceMap::Add: player %s(%d) is permanently bound to instance %d,%d,%d,%d,%d,%d but he is being put in instance %d,%d,%d,%d,%d,%d", player->GetName(), player->GetGUIDLow(), playerBind->save->GetMapId(), playerBind->save->GetInstanceId(), playerBind->save->GetDifficulty(), playerBind->save->GetPlayerCount(), playerBind->save->GetGroupCount(), playerBind->save->CanReset(), mapSave->GetMapId(), mapSave->GetInstanceId(), mapSave->GetDifficulty(), mapSave->GetPlayerCount(), mapSave->GetGroupCount(), mapSave->CanReset());
                    return false;
                    //assert(false);
                }
            }
            else
            {
                Group *pGroup = player->GetGroup();
                if (pGroup)
                {
                    // solo saves should be reset when entering a group
                    InstanceGroupBind *groupBind = pGroup->GetBoundInstance(GetId(), GetSpawnMode());
                    if (playerBind)
                    {
                        sLog.outError("InstanceMap::Add: player %s(%d) is being put in instance %d,%d,%d,%d,%d,%d but he is in group %d and is bound to instance %d,%d,%d,%d,%d,%d!", player->GetName(), player->GetGUIDLow(), mapSave->GetMapId(), mapSave->GetInstanceId(), mapSave->GetDifficulty(), mapSave->GetPlayerCount(), mapSave->GetGroupCount(), mapSave->CanReset(), GUID_LOPART(pGroup->GetLeaderGUID()), playerBind->save->GetMapId(), playerBind->save->GetInstanceId(), playerBind->save->GetDifficulty(), playerBind->save->GetPlayerCount(), playerBind->save->GetGroupCount(), playerBind->save->CanReset());
                        if (groupBind)
                            sLog.outError("InstanceMap::Add: the group is bound to instance %d,%d,%d,%d,%d,%d", groupBind->save->GetMapId(), groupBind->save->GetInstanceId(), groupBind->save->GetDifficulty(), groupBind->save->GetPlayerCount(), groupBind->save->GetGroupCount(), groupBind->save->CanReset());
                        sLog.outError("InstanceMap::Add: do not let player %s enter instance otherwise crash will happen", player->GetName());
                        return false;
                        //player->UnbindInstance(GetId(), GetSpawnMode());
                        //assert(false);
                    }
                    // bind to the group or keep using the group save
                    if (!groupBind)
                        pGroup->BindToInstance(mapSave, false);
                    else
                    {
                        // cannot jump to a different instance without resetting it
                        if(groupBind->save != mapSave)
                        {
                            sLog.outError("InstanceMap::Add: player %s(%d) is being put in instance %d,%d,%d but he is in group %d which is bound to instance %d,%d,%d!", player->GetName(), player->GetGUIDLow(), mapSave->GetMapId(), mapSave->GetInstanceId(), mapSave->GetDifficulty(), GUID_LOPART(pGroup->GetLeaderGUID()), groupBind->save->GetMapId(), groupBind->save->GetInstanceId(), groupBind->save->GetDifficulty());
                            if(mapSave)
                                sLog.outError("MapSave players: %d, group count: %d", mapSave->GetPlayerCount(), mapSave->GetGroupCount());
                            else
                                sLog.outError("MapSave NULL");
                            if(groupBind->save)
                                sLog.outError("GroupBind save players: %d, group count: %d", groupBind->save->GetPlayerCount(), groupBind->save->GetGroupCount());
                            else
                                sLog.outError("GroupBind save NULL");
                            assert(false);
                        }
                        // if the group/leader is permanently bound to the instance
                        // players also become permanently bound when they enter
                        if (groupBind->perm)
                        {
                            WorldPacket data(SMSG_INSTANCE_SAVE_CREATED, 4);
                            data << uint32(0);
                            player->GetSession()->SendPacket(&data);
                            player->BindToInstance(mapSave, true);
                        }
                    }
                }
                else
                {
                    // set up a solo bind or continue using it
                    if (!playerBind)
                        player->BindToInstance(mapSave, false);
                    else
                        // cannot jump to a different instance without resetting it
                        assert(playerBind->save == mapSave);
                }
            }

            if (i_data) i_data->OnPlayerEnter(player);
            // for normal instances cancel the reset schedule when the
            // first player enters (no players yet)
            SetResetSchedule(false);

            player->SendInitWorldStates();
            sLog.outDetail("MAP: Player '%s' entered the instance '%u' of map '%s'", player->GetName(), GetInstanceId(), GetMapName());
            // initialize unload state
            m_unloadTimer = 0;
            m_resetAfterUnload = false;
            m_unloadWhenEmpty = false;
        }


        // get or create an instance save for the map
        InstanceSave *mapSave = sInstanceSaveManager.GetInstanceSave(GetInstanceId());
        if (!mapSave)
        {
            sLog.outDetail("InstanceMap::Add: creating instance save for map %d spawnmode %d with instance id %d", GetId(), GetSpawnMode(), GetInstanceId());
            mapSave = sInstanceSaveManager.AddInstanceSave(GetId(), GetInstanceId(), GetSpawnMode(), 0, true);
        }
    }

    // this will acquire the same mutex so it cannot be in the previous block
    Map::Add(player);
    return true;
}

void InstanceMap::Update(const uint32& t_diff)
{
    Map::Update(t_diff);

    if (i_data)
        i_data->Update(t_diff);

    if (!m_unlootedCreaturesSummoned)
        SummonUnlootedCreatures();
}

void InstanceMap::Remove(Player *player, bool remove)
{
    sLog.outDetail("MAP: Removing player '%s' from instance '%u' of map '%s' before relocating to other map", player->GetName(), GetInstanceId(), GetMapName());
    //if last player set unload timer
    if (!m_unloadTimer && m_mapRefManager.getSize() == 1)
        m_unloadTimer = m_unloadWhenEmpty ? MIN_UNLOAD_DELAY : std::max(sWorld.getConfig(CONFIG_INSTANCE_UNLOAD_DELAY), (uint32)MIN_UNLOAD_DELAY);
    Map::Remove(player, remove);
    // for normal instances schedule the reset after all players have left
    SetResetSchedule(true);
}

void InstanceMap::CreateInstanceData(bool load)
{
    if (i_data != NULL)
        return;

    InstanceTemplate const* mInstance = ObjectMgr::GetInstanceTemplate(GetId());
    if (mInstance)
    {
        i_script_id = mInstance->script_id;
        i_data = sScriptMgr.CreateInstanceData(this);
    }

    if (!i_data)
        return;

    i_data->Initialize();

    if (load)
    {
        // TODO: make a global storage for this
        QueryResultAutoPtr result = CharacterDatabase.PQuery("SELECT data FROM instance WHERE map = '%u' AND id = '%u'", GetId(), i_InstanceId);
        if (result)
        {
            Field* fields = result->Fetch();
            const char* data = fields[0].GetString();
            if (data && data != "")
            {
                sLog.outDebug("Loading instance data for `%s` with id %u", sScriptMgr.GetScriptName(i_script_id), i_InstanceId);
                i_data->Load(data);
            }
        }
    }
}

/*
    Returns true if there are no players in the instance
*/
bool InstanceMap::Reset(uint8 method)
{
    // note: since the map may not be loaded when the instance needs to be reset
    // the instance must be deleted from the DB by InstanceSaveManager

    if (HavePlayers())
    {
        if (method == INSTANCE_RESET_ALL || method == INSTANCE_RESET_CHANGE_DIFFICULTY)
        {
            // notify the players to leave the instance so it can be reset
            for (MapRefManager::iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
                itr->getSource()->SendResetFailedNotify(GetId());
        }
        else
        {
            if (method == INSTANCE_RESET_GLOBAL)
            {
                // set the homebind timer for players inside (1 minute)
                for (MapRefManager::iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
                    itr->getSource()->m_InstanceValid = false;
            }

            // the unload timer is not started
            // instead the map will unload immediately after the players have left
            m_unloadWhenEmpty = true;
            m_resetAfterUnload = true;
        }
    }
    else
    {
        // unloaded at next update
        m_unloadTimer = MIN_UNLOAD_DELAY;
        m_resetAfterUnload = true;
    }

    return m_mapRefManager.isEmpty();
}

void InstanceMap::PermBindAllPlayers(Player *player)
{
    if (!IsDungeon())
        return;

    InstanceSave *save = sInstanceSaveManager.GetInstanceSave(GetInstanceId());
    if (!save)
    {
        sLog.outError("Cannot bind players, no instance save available for map!\n");
        return;
    }

    Group *group = player->GetGroup();
    // group members outside the instance group don't get bound
    for (MapRefManager::iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
    {
        Player* plr = itr->getSource();
        // players inside an instance cannot be bound to other instances
        // some players may already be permanently bound, in this case nothing happens
        InstancePlayerBind *bind = plr->GetBoundInstance(save->GetMapId(), save->GetDifficulty());
        if (!bind || !bind->perm)
        {
            plr->BindToInstance(save, true);
            WorldPacket data(SMSG_INSTANCE_SAVE_CREATED, 4);
            data << uint32(0);
            plr->GetSession()->SendPacket(&data);
        }

        // if the leader is not in the instance the group will not get a perm bind
        if (group && group->GetLeaderGUID() == plr->GetGUID())
            group->BindToInstance(save, true);
    }
}

void InstanceMap::UnloadAll()
{
    if (HavePlayers())
    {
        sLog.outError("InstanceMap::UnloadAll: there are still players in the instance at unload, should not happen!");
        for (MapRefManager::iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
        {
            Player* plr = itr->getSource();
            plr->TeleportToHomebind();
        }
    }

    if (m_resetAfterUnload == true)
        objmgr.DeleteRespawnTimeForInstance(GetInstanceId());

    Map::UnloadAll();
}

void InstanceMap::SendResetWarnings(uint32 timeLeft) const
{
    for (MapRefManager::const_iterator itr = m_mapRefManager.begin(); itr != m_mapRefManager.end(); ++itr)
        itr->getSource()->SendInstanceResetWarning(GetId(), timeLeft);
}

void InstanceMap::SetResetSchedule(bool on)
{
    // only for normal instances
    // the reset time is only scheduled when there are no players inside
    // it is assumed that the reset time will rarely (if ever) change while the reset is scheduled
    if (IsDungeon() && !HavePlayers() && !IsRaid() && !IsHeroic())
    {
        InstanceSave *save = sInstanceSaveManager.GetInstanceSave(GetInstanceId());
        if (!save)
            sLog.outError("InstanceMap::SetResetSchedule: cannot turn schedule %s, no save available for instance %d (mapid: %d)", on ? "on" : "off", GetInstanceId(), GetId());
        else
            sInstanceSaveManager.ScheduleReset(on, save->GetResetTime(), InstanceSaveManager::InstResetEvent(0, GetId(), GetInstanceId()));
    }
}

void InstanceMap::SummonUnlootedCreatures()
{
    m_unlootedCreaturesSummoned = true;
    QueryResultAutoPtr result = CharacterDatabase.PQuery("SELECT DISTINCT creatureId, position_x, position_y, position_z FROM group_saved_loot WHERE instanceId='%u' AND summoned = TRUE", GetInstanceId());
    if (result)
    {
        do
        {
            Field *fields = result->Fetch();

            uint32 creatureId = fields[0].GetUInt32();
            float pos_x = fields[1].GetFloat();
            float pos_y = fields[2].GetFloat();
            float pos_z = fields[3].GetFloat();

            TemporarySummon* pCreature = new TemporarySummon();
            if (!pCreature->Create(objmgr.GenerateLowGuid(HIGHGUID_UNIT), this, creatureId, 0, pos_x, pos_y, pos_z, 0))
            {
                delete pCreature;
                continue;
            }
            pCreature->Summon(TEMPSUMMON_MANUAL_DESPAWN, 0);
            pCreature->loot.FillLootFromDB(pCreature, NULL);
        }
        while (result->NextRow());
    }
}

uint32 InstanceMap::GetMaxPlayers() const
{
    InstanceTemplate const* iTemplate = objmgr.GetInstanceTemplate(GetId());
    if(!iTemplate)
        return 0;
    return iTemplate->maxPlayers;
}

/* ******* Battleground Instance Maps ******* */

BattleGroundMap::BattleGroundMap(uint32 id, time_t expiry, uint32 InstanceId, BattleGround *bg): Map(id, expiry, InstanceId, DIFFICULTY_NORMAL)
{
    m_bg = bg;
    BattleGroundMap::InitVisibilityDistance();

    m_VisibilityNotifyPeriod = World::GetVisibilityNotifyPeriodInBGArenas();
}

BattleGroundMap::~BattleGroundMap()
{
}

void BattleGroundMap::InitVisibilityDistance()
{
    //init visibility distance for BG/Arenas
    if (IsBattleArena())
       m_VisibleDistance = sWorld.GetMaxVisibleDistanceInArenas();
    else
        m_VisibleDistance = sWorld.GetMaxVisibleDistanceInBG();
}

bool BattleGroundMap::CanEnter(Player * player)
{
    if (player->GetMapRef().getTarget() == this)
    {
        sLog.outError("BGMap::CanEnter - player %u already in map!", player->GetGUIDLow());
        assert(false);
        return false;
    }

    if (player->GetBattleGroundId() != GetInstanceId())
        return false;

    // player number limit is checked in bgmgr, no need to do it here

    return Map::CanEnter(player);
}

void BattleGroundMap::Update(const uint32& t_diff)
{
    Map::Update(t_diff);

    if (m_bg)
        m_bg->Update(time_t(t_diff));
}

bool BattleGroundMap::Add(Player * player)
{
    {
        Guard guard(*this);
        if (!CanEnter(player))
            return false;
        // reset instance validity, battleground maps do not homebind
        player->m_InstanceValid = true;
    }
    return Map::Add(player);
}

void BattleGroundMap::Remove(Player *player, bool remove)
{
    sLog.outDetail("MAP: Removing player '%s' from bg '%u' of map '%s' before relocating to other map", player->GetName(), GetInstanceId(), GetMapName());
    Map::Remove(player, remove);
}

void BattleGroundMap::SetUnload()
{
    m_unloadTimer = MIN_UNLOAD_DELAY;
}

void BattleGroundMap::UnloadAll()
{
    while (HavePlayers())
    {
        if (Player * plr = m_mapRefManager.getFirst()->getSource())
        {
            plr->TeleportToHomebind();
            // TeleportTo removes the player from this map (if the map exists) -> calls BattleGroundMap::Remove -> invalidates the iterator.
            // just in case, remove the player from the list explicitly here as well to prevent a possible infinite loop
            // note that this remove is not needed if the code works well in other places
            plr->GetMapRef().unlink();
        }
    }

    Map::UnloadAll();
}

Creature * Map::GetCreature(uint64 guid)
{
    CreaturesMapType::const_accessor a;

    if (creaturesMap.find(a, guid))
    {
        if (a->second->GetInstanceId() != GetInstanceId())
            return NULL;
        else
            return a->second;
    }

    return NULL;
}

Creature * Map::GetCreature(uint64 guid, float x, float y)
{
    CreaturesMapType::const_accessor a;

    if (creaturesMap.find(a, guid))
    {
        CellPair p = Trinity::ComputeCellPair(x,y);
        if (p.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || p.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
        {
            sLog.outError("Map::GetCorpse: invalid coordinates supplied X:%f Y:%f grid cell [%u:%u]", x, y, p.x_coord, p.y_coord);
            return NULL;
        }

        CellPair q = Trinity::ComputeCellPair(a->second->GetPositionX(), a->second->GetPositionY());
        if (q.x_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP || q.y_coord >= TOTAL_NUMBER_OF_CELLS_PER_MAP)
        {
            sLog.outError("Map::GetCorpse: object "UI64FMTD" has invalid coordinates X:%f Y:%f grid cell [%u:%u]", a->second->GetGUID(), a->second->GetPositionX(), a->second->GetPositionY(), q.x_coord, q.y_coord);
            return NULL;
        }

        int32 dx = int32(p.x_coord) - int32(q.x_coord);
        int32 dy = int32(p.y_coord) - int32(q.y_coord);

        if (dx > -2 && dx < 2 && dy > -2 && dy < 2)
            return a->second;
        else
            return NULL;
    }

    return NULL;
}

Creature * Map::GetCreatureOrPet(uint64 guid)
{
    if (IS_PLAYER_GUID(guid))
        return NULL;

    if (IS_PET_GUID(guid))
        return ObjectAccessor::GetPet(guid);

    return GetCreature(guid);
}

GameObject * Map::GetGameObject(uint64 guid)
{
    GObjectMapType::const_accessor a;

    if (gameObjectsMap.find(a, guid))
    {
        if (a->second->GetInstanceId() != GetInstanceId())
            return NULL;
        else
            return a->second;
    }

    return NULL;
}

DynamicObject * Map::GetDynamicObject(uint64 guid)
{
    DObjectMapType::const_accessor a;

    if (dynamicObjectsMap.find(a, guid))
    {
        if (a->second->GetInstanceId() != GetInstanceId())
            return NULL;
        else
            return a->second;
    }

    return NULL;
}

Unit * Map::GetUnit(uint64 guid)
{
    if (!guid)
        return NULL;

    if (IS_PLAYER_GUID(guid))
        return ObjectAccessor::FindPlayer(guid);

    return GetCreatureOrPet(guid);
}

Object* Map::GetObjectByTypeMask(Player const &p, uint64 guid, uint32 typemask)
{
    Object *obj = NULL;

    if (typemask & TYPEMASK_PLAYER)
    {
        obj = ObjectAccessor::FindPlayer(guid);
        if (obj) return obj;
    }

    if (typemask & TYPEMASK_UNIT)
    {
        obj = GetCreatureOrPet(guid);
        if (obj) return obj;
    }

    if (typemask & TYPEMASK_GAMEOBJECT)
    {
        obj = GetGameObject(guid);
        if (obj) return obj;
    }

    if (typemask & TYPEMASK_DYNAMICOBJECT)
    {
        obj = GetDynamicObject(guid);
        if (obj) return obj;
    }

    if (typemask & TYPEMASK_ITEM)
    {
        obj = p.GetItemByGuid(guid);
        if (obj) return obj;
    }

    return NULL;
}

std::list<uint64> Map::GetCreaturesGUIDList(uint32 id, GetCreatureGuidType type , uint32 max)
{
    std::list<uint64> returnList;
    CreatureIdToGuidListMapType::const_accessor a;
    if (creatureIdToGuidMap.find(a, id))
    {
        std::list<uint64> tmpList = a->second;

        if (!max || max > tmpList.size())
        {
            max = tmpList.size();
            if (type == GET_RANDOM_CREATURE_GUID)
                type = GET_FIRST_CREATURE_GUID;
        }
        uint64 count = 0;
        switch (type)
        {
            case GET_FIRST_CREATURE_GUID:
                for (std::list<uint64>::iterator itr = tmpList.begin(); count != max; ++itr, ++count)
                    returnList.push_back(*itr);
                break;
            case GET_LAST_CREATURE_GUID:
                for (std::list<uint64>::reverse_iterator itr = tmpList.rbegin(); count != max; ++itr, ++count)
                    returnList.push_back(*itr);
                break;
            case GET_RANDOM_CREATURE_GUID:
                for (count = 0; count != max; ++count)
                {
                    std::list<uint64>::iterator itr = tmpList.begin();
                    std::advance(itr, rand()%(tmpList.size()-1));
                    returnList.push_back(*itr);
                    tmpList.erase(itr);
                }
                break;
        }
    }

    return returnList;
}

uint64 Map::GetCreatureGUID(uint32 id, GetCreatureGuidType type)
{
    uint64 returnGUID = 0;

    CreatureIdToGuidListMapType::const_accessor a;
    if (creatureIdToGuidMap.find(a, id))
    {
        switch (type)
        {
            case GET_FIRST_CREATURE_GUID:
                returnGUID = a->second.front();
                break;
            case GET_LAST_CREATURE_GUID:
                returnGUID = a->second.back();
                break;
            case GET_RANDOM_CREATURE_GUID:
                std::list<uint64>::const_iterator itr= a->second.begin();
                std::advance(itr, urand(0, a->second.size()-1));
                returnGUID = *itr;
                break;
        }
    }

    return returnGUID;
}

void Map::InsertIntoCreatureGUIDList(Creature * obj)
{
    CreatureIdToGuidListMapType::accessor a;
    if (creatureIdToGuidMap.insert(a, obj->GetEntry()))
    {
        std::list<uint64> tmp;
        tmp.push_back(obj->GetGUID());
        a->second = tmp;
    }
    else
    {
        a.release();
        if (creatureIdToGuidMap.find(a, obj->GetEntry()))
            a->second.push_back(obj->GetGUID());
    }
}

void Map::RemoveFromCreatureGUIDList(Creature * obj)
{
    CreatureIdToGuidListMapType::accessor a;
    if (creatureIdToGuidMap.find(a, obj->GetEntry()))
        a->second.remove(obj->GetGUID());
}


void Map::InsertIntoObjMap(Object * obj)
{
    ObjectGuid guid(obj->GetGUID());

    switch (guid.GetHigh())
    {
        case HIGHGUID_UNIT:
            {
                CreaturesMapType::accessor a;

                if (creaturesMap.insert(a, guid.GetRawValue()))
                {
                    a->second = (Creature*)obj;
                    InsertIntoCreatureGUIDList(a->second);
                }
                else
                    error_log("Map::InsertIntoCreatureMap: GUID %u already in map", guid.GetRawValue());

                a.release();
                break;
            }
        case HIGHGUID_GAMEOBJECT:
            {
                GObjectMapType::accessor a;

                if (gameObjectsMap.insert(a, guid.GetRawValue()))
                    a->second = (GameObject*)obj;
                else
                    error_log("Map::InsertIntoGameObjectMap: GUID %u already in map", guid.GetRawValue());

                a.release();
                break;
            }
        case HIGHGUID_DYNAMICOBJECT:
            {
                DObjectMapType::accessor a;

                if (dynamicObjectsMap.insert(a, guid.GetRawValue()))
                    a->second = (DynamicObject*)obj;
                else
                    error_log("Map::InsertIntoDynamicObjectMap: GUID %u already in map", guid.GetRawValue());

                a.release();
                break;
            }
        case HIGHGUID_PET:
            ObjectAccessor::Instance().AddPet((Pet*)obj);
            break;

        case HIGHGUID_PLAYER:
            ObjectAccessor::Instance().AddPlayer((Player*)obj);
            break;

        case HIGHGUID_CORPSE:
            ObjectAccessor::Instance().AddCorpse((Corpse*)obj);
            break;
        default:
            break;
    }
}

void Map::RemoveFromObjMap(uint64 guid)
{
    ObjectGuid objGuid(guid);

    switch (objGuid.GetHigh())
    {
        case HIGHGUID_UNIT:
            if (!creaturesMap.erase(guid))
                error_log("Map::RemoveFromCreatureMap: Creature GUID %u not in map", guid);
            break;

        case HIGHGUID_GAMEOBJECT:
            if (!gameObjectsMap.erase(guid))
                error_log("Map::RemoveFromGameObjectMap: Game Object GUID %u not in map", guid);
            break;

        case HIGHGUID_DYNAMICOBJECT:
            if (!dynamicObjectsMap.erase(guid))
                error_log("Map::RemoveFromDynamicObjectMap: Dynamic Object GUID %u not in map", guid);
            break;

        case HIGHGUID_PET:
            ObjectAccessor::Instance().RemovePet(guid);
            break;

        case HIGHGUID_PLAYER:
            ObjectAccessor::Instance().RemovePlayer(guid);
            break;

        case HIGHGUID_CORPSE:
            HashMapHolder<Corpse>::Remove(guid);
            break;
        default:
            break;
    }
}

void Map::RemoveFromObjMap(Object * obj)
{
    ObjectGuid objGuid(obj->GetGUID());

    switch (objGuid.GetHigh())
    {
        case HIGHGUID_UNIT:
            RemoveFromCreatureGUIDList((Creature*)obj);
            if (!creaturesMap.erase(objGuid.GetRawValue()))
                error_log("Map::RemoveFromCreatureMap: Creature GUID %u not in map", objGuid.GetRawValue());
            break;

        case HIGHGUID_GAMEOBJECT:
            if (!gameObjectsMap.erase(objGuid.GetRawValue()))
                error_log("Map::RemoveFromGameObjectMap: Game Object GUID %u not in map", objGuid.GetRawValue());
            break;

        case HIGHGUID_DYNAMICOBJECT:
            if (!dynamicObjectsMap.erase(objGuid.GetRawValue()))
                error_log("Map::RemoveFromDynamicObjectMap: Dynamic Object GUID %u not in map", objGuid.GetRawValue());
            break;

        case HIGHGUID_PET:
            ObjectAccessor::Instance().RemovePet(objGuid.GetRawValue());
            break;

        case HIGHGUID_PLAYER:
            ObjectAccessor::Instance().RemovePlayer(objGuid.GetRawValue());
            break;

        case HIGHGUID_CORPSE:
            HashMapHolder<Corpse>::Remove(objGuid.GetRawValue());
            break;
        default:
            break;
    }
}

inline GridMap *Map::GetGrid(float x, float y)
{
    // half opt method
    int gx=(int)(32-x/SIZE_OF_GRIDS);                       //grid x
    int gy=(int)(32-y/SIZE_OF_GRIDS);                       //grid y

    // ensure GridMap is loaded
    EnsureGridCreated(GridPair(63-gx,63-gy));

    return GridMaps[gx][gy];
}

void Map::ForcedUnload()
{
    sLog.outError("Map::ForcedUnload called for map %u instance %u. Map crushed. Cleaning up...", GetId(), GetInstanceId());
    sLog.outCrash("Map::ForcedUnload called for map %u instance %u. Map crushed. Cleaning up...", GetId(), GetInstanceId());

    // Immediately cleanup update sets/queues
    i_objectsToClientUpdate.clear();

    Map::PlayerList const pList = GetPlayers();

    for (PlayerList::const_iterator itr = pList.begin(); itr != pList.end(); ++itr)
    {
        Player* player = itr->getSource();
        if (!player || !player->GetSession())
            continue;

        if (player->IsBeingTeleported())
        {
            WorldLocation old_loc;
            player->GetPosition(old_loc);
            if (!player->TeleportTo(old_loc))
            {
                sLog.outDetail("Map::ForcedUnload: %u is in teleport state, cannot be ported to his previous place, teleporting him to his homebind place...",
                    player->GetGUIDLow());
                player->TeleportToHomebind();
            }
            player->SetSemaphoreTeleport(false);
        }

        switch (sWorld.getConfig(CONFIG_VMSS_MAPFREEMETHOD))
        {
            case 0:
            {
                player->RemoveAllAurasOnDeath();
                if (Pet* pet = player->GetPet())
                    pet->RemoveAllAurasOnDeath();
                player->GetSession()->LogoutPlayer(true);
                break;
            }
            case 1:
            {
                player->GetSession()->KickPlayer();
                break;
            }
            case 2:
            {
                player->GetSession()->LogoutPlayer(false);
                break;
            }
            default:
                break;
        }
    }

    switch (sWorld.getConfig(CONFIG_VMSS_MAPFREEMETHOD))
    {
        case 0:
            if (InstanceMap *instance = dynamic_cast<InstanceMap*>(this))
                if (InstanceData* iData = instance->GetInstanceData())
                    iData->SaveToDB();
            break;
        default:
            break;
    }

    UnloadAll();

    SetBroken(false);
}

float Map::GetVisibilityDistance(WorldObject* obj) const
{
    if (obj)
    {
        if (obj->GetObjectGuid().IsGameObject())
            return (m_VisibleDistance + ((GameObject*)obj)->GetDeterminativeSize());    // or maybe should be GetMaxVisibleDistanceForObject instead m_VisibleDistance ?
        else if(obj->GetObjectGuid().IsCreature())
            return (m_VisibleDistance + ((Creature*)obj)->GetDeterminativeSize());
    }

    return m_VisibleDistance;
}
