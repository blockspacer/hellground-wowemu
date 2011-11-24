/*
 * Copyright (C) 2005-2008 MaNGOS <http://www.mangosproject.org/>
 *
 * Copyright (C) 2008 Trinity <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef TRINITY_WAYPOINTMOVEMENTGENERATOR_H
#define TRINITY_WAYPOINTMOVEMENTGENERATOR_H

/** @page PathMovementGenerator is used to generate movements
 * of waypoints and flight paths.  Each serves the purpose
 * of generate activities so that it generates updated
 * packets for the players.
 */

#include "MovementGenerator.h"
#include "DestinationHolder.h"
#include "WaypointManager.h"
#include "Path.h"
#include "Traveller.h"

#include "Player.h"

#include <vector>
#include <set>

#define FLIGHT_TRAVEL_UPDATE  100
#define STOP_TIME_FOR_PLAYER  3 * 60 * 1000                         // 3 Minutes
#define TIMEDIFF_NEXT_WP      250

template<class T, class P>
class TRINITY_DLL_SPEC PathMovementBase
{
    public:
        PathMovementBase() : i_currentNode(0) {}
        virtual ~PathMovementBase() {};

        bool MovementInProgress(void) const { return i_currentNode < i_path->size(); }

        // template pattern, not defined .. override required
        void LoadPath(T &);
        uint32 GetCurrentNode() const { return i_currentNode; }

        bool GetDestination(float& x, float& y, float& z) const { i_destinationHolder.GetDestination(x,y,z); return true; }
        bool GetPosition(float& x, float& y, float& z) const { i_destinationHolder.GetLocationNowNoMicroMovement(x,y,z); return true; }
    protected:
        uint32 i_currentNode;
        DestinationHolder< Traveller<T> > i_destinationHolder;
        P i_path;
};

template<class T>

class TRINITY_DLL_SPEC WaypointMovementGenerator
    : public MovementGeneratorMedium< T, WaypointMovementGenerator<T> >, public PathMovementBase<T, WaypointPath const*>
{
    public:
        WaypointMovementGenerator(uint32 _path_id = 0, bool _repeating = true) :
          i_nextMoveTime(0), path_id(_path_id), repeating(_repeating), StopedByPlayer(false), node(NULL) {}

        void Initialize(T &);
        void Finalize(T &);
        void MovementInform(T &);
        void InitTraveller(T &, const WaypointData &);
        void GeneratePathId(T &);
        void Reset(T &unit);
        bool Update(T &, const uint32 &);
        bool GetDestination(float &x, float &y, float &z) const;
        MovementGeneratorType GetMovementGeneratorType() { return WAYPOINT_MOTION_TYPE; }

    private:
        WaypointData *node;
        uint32 path_id;
        TimeTrackerSmall i_nextMoveTime;
        WaypointPath *waypoints;
        bool repeating, StopedByPlayer;
};

/** FlightPathMovementGenerator generates movement of the player for the paths
 * and hence generates ground and activities for the player.
 */
class TRINITY_DLL_SPEC FlightPathMovementGenerator
: public MovementGeneratorMedium< Player, FlightPathMovementGenerator >,
public PathMovementBase<Player,TaxiPathNodeList const*>
{
    public:
        explicit FlightPathMovementGenerator(TaxiPathNodeList const& pathnodes, uint32 startNode = 0)
        {
            i_path = &pathnodes;
            i_currentNode = startNode;
        }
        void Initialize(Player &);
        void Finalize(Player &);
        void Interrupt(Player &);
        void Reset(Player &);
        bool Update(Player &, const uint32 &);
        MovementGeneratorType GetMovementGeneratorType() { return FLIGHT_MOTION_TYPE; }

        TaxiPathNodeList const& GetPath() { return *i_path; }
        uint32 GetPathAtMapEnd() const;
        bool HasArrived() const { return (i_currentNode >= i_path->size()); }
        void SetCurrentNodeAfterTeleport();
        void SkipCurrentNode() { ++i_currentNode; }
        void DoEventIfAny(Player& player, TaxiPathNodeEntry const& node, bool departure);

        // allow use for overwrite empty implementation
        bool GetDestination(float& x, float& y, float& z) const { return PathMovementBase<Player,TaxiPathNodeList const*>::GetDestination(x,y,z); }
};

#endif
