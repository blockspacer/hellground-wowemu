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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include "MotionMaster.h"
#include "CreatureAISelector.h"
#include "Creature.h"
#include "Traveller.h"

#include "ConfusedMovementGenerator.h"
#include "FleeingMovementGenerator.h"
#include "HomeMovementGenerator.h"
#include "IdleMovementGenerator.h"
#include "PointMovementGenerator.h"
#include "TargetedMovementGenerator.h"
#include "WaypointMovementGenerator.h"
#include "RandomMovementGenerator.h"

#include <cassert>

inline bool isStatic(MovementGenerator *mv)
{
    return (mv == &si_idleMovement);
}

void MotionMaster::Initialize()
{
    // clear ALL movement generators (including default)
    while (!empty())
    {
        MovementGenerator *curr = top();

        pop();

        if (curr)
            DirectDelete(curr);
    }

    // set new default movement generator
    InitDefault();
}

void MotionMaster::InitDefault()
{
    if (i_owner->GetTypeId() == TYPEID_UNIT)
    {
        MovementGenerator* movement = FactorySelector::selectMovementGenerator((Creature*)i_owner);
        Mutate(movement == NULL ? &si_idleMovement : movement, MOTION_SLOT_IDLE);
    }
    else
    {
        Mutate(&si_idleMovement, MOTION_SLOT_IDLE);
    }
}

MotionMaster::~MotionMaster()
{
    // clear ALL movement generators (including default)
    while (!empty())
    {
        MovementGenerator *curr = top();

        pop();

        if (curr)
            DirectDelete(curr);
    }
}

void MotionMaster::UpdateMotion(uint32 diff)
{
    if (i_owner->hasUnitState(UNIT_STAT_ROOT | UNIT_STAT_STUNNED))
        return;

    assert(!empty());

    m_cleanFlag |= MMCF_UPDATE;

    if (!top()->Update(*i_owner, diff))
    {
        m_cleanFlag &= ~MMCF_UPDATE;
        MovementExpired();
    }
    else
        m_cleanFlag &= ~MMCF_UPDATE;

    if (m_expList)
    {
        for (int i = 0; i < m_expList->size(); ++i)
        {
            MovementGenerator* mg = (*m_expList)[i];
            DirectDelete(mg);
        }

        delete m_expList;
        m_expList = NULL;

        if (empty())
        {
            Initialize();
        }
        else if (needInitTop())
        {
            InitTop();
        }
        else if (m_cleanFlag & MMCF_RESET)
        {
            top()->Reset(*i_owner);
        }

        m_cleanFlag &= ~MMCF_RESET;
    }
}

void MotionMaster::DirectClean(bool reset)
{
    while (size() > 1)
    {
        MovementGenerator *curr = top();

        pop();

        if (curr)
            DirectDelete(curr);
    }

    if (needInitTop())
    {
        InitTop();
    }
    else if (reset)
    {
        top()->Reset(*i_owner);
    }
}

void MotionMaster::DelayedClean()
{
    while (size() > 1)
    {
        MovementGenerator *curr = top();

        pop();

        if (curr)
            DelayedDelete(curr);
    }
}

void MotionMaster::DirectExpire(bool reset)
{
    if (size() > 1)
    {
        MovementGenerator *curr = top();

        pop();

        DirectDelete(curr);
    }

    while (!top())
    {
        --i_top;
    }

    if (empty())
    {
        Initialize();
    }
    else if (needInitTop())
    {
        InitTop();
    }
    else if (reset)
    {
        top()->Reset(*i_owner);
    }
}

void MotionMaster::DelayedExpire()
{
    if (size() > 1)
    {
        MovementGenerator *curr = top();

        pop();

        DelayedDelete(curr);
    }

    while (!top())
    {
        --i_top;
    }
}

void MotionMaster::MoveIdle(MovementSlot slot)
{
    if (!isStatic(Impl[slot]))
    {
        Mutate(&si_idleMovement, slot);
    }
}

void MotionMaster::MoveRandom(float spawndist)
{
    if (i_owner->GetTypeId() == TYPEID_UNIT)
    {
        Mutate(new RandomMovementGenerator<Creature>(spawndist), MOTION_SLOT_IDLE);
    }
}

void MotionMaster::MoveTargetedHome()
{
    Clear(false);

    if (i_owner->GetTypeId()==TYPEID_UNIT && !((Creature*)i_owner)->GetCharmerOrOwnerGUID())
    {
        Mutate(new HomeMovementGenerator<Creature>(), MOTION_SLOT_ACTIVE);
    }
    else if (i_owner->GetTypeId()==TYPEID_UNIT && ((Creature*)i_owner)->GetCharmerOrOwnerGUID())
    {
        if (Unit *target = ((Creature*)i_owner)->GetCharmerOrOwner())
        {
            i_owner->addUnitState(UNIT_STAT_FOLLOW);

            Mutate(new TargetedMovementGenerator<Creature>(*target,PET_FOLLOW_DIST,PET_FOLLOW_ANGLE), MOTION_SLOT_ACTIVE);
        }
    }
    else
    {
        sLog.outError("Player (GUID: %u) attempt targeted home", i_owner->GetGUIDLow());
    }
}

void MotionMaster::MoveConfused()
{
    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        Mutate(new ConfusedMovementGenerator<Player>(), MOTION_SLOT_CONTROLLED);
    }
    else
    {
        Mutate(new ConfusedMovementGenerator<Creature>(), MOTION_SLOT_CONTROLLED);
    }
}

void MotionMaster::MoveChase(Unit* target, float dist, float angle)
{
    // ignore movement request if target not exist
    if (!target || target == i_owner)
        return;

    if (i_owner->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_MOVE))
        return;

    i_owner->clearUnitState(UNIT_STAT_FOLLOW);

    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        Mutate(new TargetedMovementGenerator<Player>(*target,dist,angle), MOTION_SLOT_ACTIVE);
    }
    else
    {
        Mutate(new TargetedMovementGenerator<Creature>(*target,dist,angle), MOTION_SLOT_ACTIVE);
    }
}

void MotionMaster::MoveFollow(Unit* target, float dist, float angle, MovementSlot slot)
{
    // ignore movement request if target not exist
    if (!target || target == i_owner)
        return;

    if (i_owner->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_DISABLE_MOVE))
        return;

    i_owner->addUnitState(UNIT_STAT_FOLLOW);

    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        Mutate(new TargetedMovementGenerator<Player>(*target,dist,angle), slot);
    }
    else
    {
        Mutate(new TargetedMovementGenerator<Creature>(*target,dist,angle), slot);
    }
}

void MotionMaster::MovePoint(uint32 id, float x, float y, float z)
{
    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        Mutate(new PointMovementGenerator<Player>(id,x,y,z), MOTION_SLOT_ACTIVE);
    }
    else
    {
        Mutate(new PointMovementGenerator<Creature>(id,x,y,z), MOTION_SLOT_ACTIVE);
    }
}

void MotionMaster::MoveCharge(float x, float y, float z, float speed, uint32 id)
{
    if (Impl[MOTION_SLOT_CONTROLLED] && Impl[MOTION_SLOT_CONTROLLED]->GetMovementGeneratorType() != DISTRACT_MOTION_TYPE)
        return;

    i_owner->addUnitState(UNIT_STAT_CHARGING);
    i_owner->m_TempSpeed = speed;

    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        Mutate(new PointMovementGenerator<Player>(id,x,y,z), MOTION_SLOT_CONTROLLED);
    }
    else
    {
       Mutate(new PointMovementGenerator<Creature>(id,x,y,z), MOTION_SLOT_CONTROLLED);
    }
}

void MotionMaster::MoveFall(float z, uint32 id)
{
    i_owner->SetFlying(false);
    i_owner->SendMovementFlagUpdate();

    MoveCharge(i_owner->GetPositionX(), i_owner->GetPositionY(), z, SPEED_CHARGE, id);
}

void MotionMaster::MoveSeekAssistance(float x, float y, float z)
{
    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        sLog.outError("Player (GUID: %u) attempt to seek assistance",i_owner->GetGUIDLow());
    }
    else
    {
        i_owner->AttackStop();
        ((Creature*)i_owner)->SetReactState(REACT_PASSIVE);

        Mutate(new AssistanceMovementGenerator(x,y,z), MOTION_SLOT_ACTIVE);
    }
}

void MotionMaster::MoveSeekAssistanceDistract(uint32 time)
{
    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        sLog.outError("Player (GUID: %u) attempt to call distract after assistance",i_owner->GetGUIDLow());
    }
    else
    {
        Mutate(new AssistanceDistractMovementGenerator(time), MOTION_SLOT_ACTIVE);
    }
}

void MotionMaster::MoveFleeing(Unit* enemy, uint32 time)
{
    if (!enemy)
        return;

    if (i_owner->HasAuraType(SPELL_AURA_PREVENTS_FLEEING))
        return;

    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        Mutate(new FleeingMovementGenerator<Player>(enemy->GetGUID()), MOTION_SLOT_CONTROLLED);
    }
    else
    {
        if (time)
        {
            Mutate(new TimedFleeingMovementGenerator(enemy->GetGUID(), time), MOTION_SLOT_CONTROLLED);
        }
        else
        {
            Mutate(new FleeingMovementGenerator<Creature>(enemy->GetGUID()), MOTION_SLOT_CONTROLLED);
        }
    }
}

void MotionMaster::MoveTaxiFlight(uint32 path, uint32 pathnode)
{
    if (i_owner->GetTypeId() == TYPEID_PLAYER)
    {
        FlightPathMovementGenerator* mgen = new FlightPathMovementGenerator(sTaxiPathNodesByPath[path],pathnode);
        Mutate(mgen, MOTION_SLOT_CONTROLLED);
    }
    else
    {
        sLog.outError("Creature (Entry: %u GUID: %u) attempt taxi to (Path %u node %u)", i_owner->GetEntry(), i_owner->GetGUIDLow(), path, pathnode);
    }
}

void MotionMaster::MoveDistract(uint32 timer)
{
    if (Impl[MOTION_SLOT_CONTROLLED])
        return;

    DistractMovementGenerator* mgen = new DistractMovementGenerator(timer);
    Mutate(mgen, MOTION_SLOT_CONTROLLED);
}

void MotionMaster::MoveRotate(uint32 time, RotateDirection direction)
{
    if (!time)
        return;

    Mutate(new RotateMovementGenerator(time, direction), MOTION_SLOT_ACTIVE);
}

void MotionMaster::Mutate(MovementGenerator *m, MovementSlot slot)
{
    if (MovementGenerator *curr = Impl[slot])
    {
        Impl[slot] = NULL;                               // in case a new one is generated in this slot during directdelete
        if (i_top == slot && (m_cleanFlag & MMCF_UPDATE))
            DelayedDelete(curr);
        else
            DirectDelete(curr);
    }
    else if (i_top < slot)
    {
        i_top = slot;
    }

    if (i_top > slot)
        needInit[slot] = true;
    else
    {
        m->Initialize(*i_owner);
        needInit[slot] = false;
    }

    Impl[slot] = m;
}

void MotionMaster::MovePath(uint32 path_id, bool repeatable)
{
    if (!path_id)
        return;

    Mutate(new WaypointMovementGenerator<Creature>(path_id, repeatable), MOTION_SLOT_IDLE);
}

void MotionMaster::propagateSpeedChange()
{
    for (int i = 0; i <= i_top; ++i)
    {
        if (Impl[i])
            Impl[i]->unitSpeedChanged();
    }
}

MovementGeneratorType MotionMaster::GetCurrentMovementGeneratorType() const
{
   if (empty() || !top())
       return IDLE_MOTION_TYPE;

   return top()->GetMovementGeneratorType();
}

MovementGeneratorType MotionMaster::GetMotionSlotType(int slot) const
{
    if (!Impl[slot])
        return NULL_MOTION_TYPE;
    else
        return Impl[slot]->GetMovementGeneratorType();
}

void MotionMaster::InitTop()
{
    top()->Initialize(*i_owner);
    needInit[i_top] = false;
}

void MotionMaster::DirectDelete(_Ty curr)
{
    if (isStatic(curr))
        return;

    curr->Finalize(*i_owner);
    delete curr;
}

void MotionMaster::DelayedDelete(_Ty curr)
{
    sLog.outError("CRASH ALARM! Unit (Entry %u) is trying to delete its updating MG (Type %u)!", i_owner->GetEntry(), curr->GetMovementGeneratorType());

    if (isStatic(curr))
        return;

    if (!m_expList)
        m_expList = new ExpireList();

    m_expList->push_back(curr);
}

bool MotionMaster::GetDestination(float &x, float &y, float &z)
{
   if (empty())
       return false;

   return top() ? top()->GetDestination(x,y,z) : false;
}

