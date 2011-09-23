/*
 * Copyright (C) 2005-2010 MaNGOS <http://getmangos.com/>
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

#include "Creature.h"
#include "MapManager.h"
#include "RandomMovementGenerator.h"
#include "DestinationHolderImp.h"
#include "Map.h"
#include "Util.h"

template<>
void RandomMovementGenerator<Creature>::_setRandomLocation(Creature &creature)
{
    float respX, respY, respZ, respO, currZ, destX, destY, destZ, wander_distance, travelDistZ;

    creature.GetRespawnCoord(respX, respY, respZ, &respO, &wander_distance);

    currZ = creature.GetPositionZ();
    Map const* map = creature.GetBaseMap();

    // For 2D/3D system selection
    //bool is_land_ok  = creature.CanWalk();                // not used?
    //bool is_water_ok = creature.CanSwim();                // not used?
    bool is_air_ok = creature.CanFly();

    const float angle = rand_norm() * (M_PI*2.0f);
    const float range = rand_norm() * wander_distance;
    const float distanceX = range * cos(angle);
    const float distanceY = range * sin(angle);

    destX = respX + distanceX;
    destY = respY + distanceY;

    // prevent invalid coordinates generation
    Trinity::NormalizeMapCoord(destX);
    Trinity::NormalizeMapCoord(destY);

    travelDistZ = distanceX*distanceX + distanceY*distanceY;

    if (is_air_ok)                                          // 3D system above ground and above water (flying mode)
    {
        // Limit height change
        const float distanceZ = rand_norm() * sqrtf(travelDistZ)/2.0f;
        destZ = respZ + distanceZ;
        float levelZ = map->GetWaterOrGroundLevel(destX, destY, destZ-2.0f);

        // Problem here, we must fly above the ground and water, not under. Let's try on next tick
        if (levelZ >= destZ)
            return;
    }
    //else if (is_water_ok)                                 // 3D system under water and above ground (swimming mode)
    else                                                    // 2D only
    {
        // 10.0 is the max that vmap high can check (MAX_CAN_FALL_DISTANCE)
        travelDistZ = travelDistZ >= 100.0f ? 10.0f : sqrtf(travelDistZ);

        // The fastest way to get an accurate result 90% of the time.
        // Better result can be obtained like 99% accuracy with a ray light, but the cost is too high and the code is too long.
        destZ = map->GetHeight(destX, destY, respZ+travelDistZ-2.0f, false);

        if (fabs(destZ - respZ) > travelDistZ)              // Map check
        {
            // Vmap Horizontal or above
            destZ = map->GetHeight(destX, destY, respZ - 2.0f, true);

            if (fabs(destZ - respZ) > travelDistZ)
            {
                // Vmap Higher
                destZ = map->GetHeight(destX, destY, respZ+travelDistZ-2.0f, true);

                // let's forget this bad coords where a z cannot be find and retry at next tick
                if (fabs(destZ - respZ) > travelDistZ)
                    return;
            }
        }
    }

    Traveller<Creature> traveller(creature);

    creature.SetOrientation(creature.GetAngle(destX, destY));
    i_destinationHolder.SetDestination(traveller, destX, destY, destZ);
    creature.addUnitState(UNIT_STAT_ROAMING);

    if (is_air_ok)
    {
        i_nextMoveTime.Reset(i_destinationHolder.GetTotalTravelTime());
        creature.AddUnitMovementFlag(SPLINEFLAG_FLYINGING2);
    }
    //else if (is_water_ok)                                 // Swimming mode to be done with more than this check
    else
    {
        i_nextMoveTime.Reset(i_destinationHolder.GetTotalTravelTime() + urand(500, 10000));
        creature.AddUnitMovementFlag(SPLINEFLAG_WALKMODE_MODE);
    }
}

template<>
void RandomMovementGenerator<Creature>::Initialize(Creature &creature)
{
    if (!creature.isAlive())
        return;

    if (creature.CanFly())
        creature.AddUnitMovementFlag(SPLINEFLAG_FLYINGING2);
    else
        creature.AddUnitMovementFlag(SPLINEFLAG_WALKMODE_MODE);

    _setRandomLocation(creature);
}

template<>
void RandomMovementGenerator<Creature>::Reset(Creature &creature)
{
    Initialize(creature);
}

template<>
bool RandomMovementGenerator<Creature>::Update(Creature &creature, const uint32 &diff)
{
    if (creature.hasUnitState(UNIT_STAT_ROOT | UNIT_STAT_STUNNED | UNIT_STAT_DISTRACTED | UNIT_STAT_DIED))
    {
        i_nextMoveTime.Update(i_nextMoveTime.GetExpiry());  // Expire the timer
        creature.clearUnitState(UNIT_STAT_ROAMING);
        return true;
    }

    i_nextMoveTime.Update(diff);

    if (i_destinationHolder.HasArrived() && !creature.IsStopped() && !creature.CanFly())
        creature.clearUnitState(UNIT_STAT_ROAMING);

    if (!i_destinationHolder.HasArrived() && creature.IsStopped())
        creature.addUnitState(UNIT_STAT_ROAMING);

    CreatureTraveller traveller(creature);

    if (i_destinationHolder.UpdateTraveller(traveller, diff, true))
    {
        if (i_nextMoveTime.Passed())
        {
            if (creature.CanFly())
                creature.AddUnitMovementFlag(SPLINEFLAG_FLYINGING2);
            else
                creature.AddUnitMovementFlag(SPLINEFLAG_WALKMODE_MODE);

            _setRandomLocation(creature);
        }
        else if (creature.isPet() && creature.GetOwner() && !creature.IsWithinDist(creature.GetOwner(), PET_FOLLOW_DIST+2.5f))
        {
           creature.AddUnitMovementFlag(SPLINEFLAG_WALKMODE_MODE);
           _setRandomLocation(creature);
        }
    }
    return true;
}
