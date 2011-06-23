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

#include "Common.h"
#include "QuestDef.h"
#include "GameObject.h"
#include "ObjectMgr.h"
#include "PoolHandler.h"
#include "SpellMgr.h"
#include "Spell.h"
#include "UpdateMask.h"
#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "World.h"
#include "Database/DatabaseEnv.h"
#include "MapManager.h"
#include "LootMgr.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "InstanceData.h"
#include "BattleGround.h"
#include "Util.h"
#include "OutdoorPvPMgr.h"
#include "BattleGroundAV.h"
#include "Map.h"

GameObject::GameObject() : WorldObject()
{
    m_objectType |= TYPEMASK_GAMEOBJECT;
    m_objectTypeId = TYPEID_GAMEOBJECT;
                                                            // 2.3.2 - 0x58
    m_updateFlag = (UPDATEFLAG_LOWGUID | UPDATEFLAG_HIGHGUID | UPDATEFLAG_HAS_POSITION);

    m_valuesCount = GAMEOBJECT_END;
    m_respawnTime = 0;
    m_respawnDelayTime = 25;
    m_lootState = GO_NOT_READY;
    m_spawnedByDefault = true;
    m_usetimes = 0;
    m_spellId = 0;
    m_charges = 5;
    m_cooldownTime = 0;
    m_goInfo = NULL;
    m_goData = NULL;

    m_DBTableGuid = 0;
}

GameObject::~GameObject()
{
    if (m_uint32Values)                                      // field array can be not exist if GameOBject not loaded
    {
        // crash possible at access to deleted GO in Unit::m_gameobj
        uint64 owner_guid = GetOwnerGUID();
        if (owner_guid)
        {
            Unit* owner = NULL;
            if (IS_PLAYER_GUID(owner_guid))
                owner = ObjectAccessor::GetPlayer(owner_guid);
            else
                owner = GetMap()->GetUnit(owner_guid);

            if (owner)
                owner->RemoveGameObject(this,false);
            else
            {
                const char * ownerType = "creature";
                if (IS_PLAYER_GUID(owner_guid))
                    ownerType = "player";
                else if (IS_PET_GUID(owner_guid))
                    ownerType = "pet";

                sLog.outError("Delete GameObject (GUID: %u Entry: %u SpellId %u LinkedGO %u) that lost references to owner (GUID %u Type '%s') GO list. Crash possible later.",
                    GetGUIDLow(), GetGOInfo()->id, m_spellId, GetLinkedGameObjectEntry(), GUID_LOPART(owner_guid), ownerType);
            }
        }
    }
}

void GameObject::SendCustomAnimation()
{
    WorldPacket data(SMSG_GAMEOBJECT_CUSTOM_ANIM,8+4);
    data << GetGUID();
    data << (uint32)(GetGoAnimProgress());
    SendMessageToSet(&data, false);
}

void GameObject::SendSpawnAnimation()
{
    WorldPacket data(SMSG_GAMEOBJECT_SPAWN_ANIM_OBSOLETE, 8);
    data << GetGUID();
    SendMessageToSet(&data, true);
}

void GameObject::AddToWorld()
{
    ///- Register the gameobject for guid lookup
    if (!IsInWorld())
    {
        GetMap()->InsertIntoObjMap(this);
        WorldObject::AddToWorld();

        if (m_zoneScript)
            m_zoneScript->OnGameObjectCreate(this, true);
    }
}

void GameObject::RemoveFromWorld()
{
    ///- Remove the gameobject from the accessor
    if (IsInWorld())
    {
        if (m_zoneScript)
            m_zoneScript->OnGameObjectCreate(this, false);

        WorldObject::RemoveFromWorld();
        GetMap()->RemoveFromObjMap(GetGUID());
    }
}

bool GameObject::Create(uint32 guidlow, uint32 name_id, Map *map, float x, float y, float z, float ang, float rotation0, float rotation1, float rotation2, float rotation3, uint32 animprogress, GOState go_state, uint32 ArtKit)
{
    Relocate(x,y,z,ang);
    SetMapId(map->GetId());
    SetInstanceId(map->GetInstanceId());

    if (!IsPositionValid())
    {
        sLog.outError("ERROR: Gameobject (GUID: %u Entry: %u) not created. Suggested coordinates isn't valid (X: %f Y: %f)",guidlow,name_id,x,y);
        return false;
    }

    GameObjectInfo const* goinfo = objmgr.GetGameObjectInfo(name_id);
    if (!goinfo)
    {
        sLog.outErrorDb("Gameobject (GUID: %u Entry: %u) not created: it have not exist entry in `gameobject_template`. Map: %u  (X: %f Y: %f Z: %f) ang: %f rotation0: %f rotation1: %f rotation2: %f rotation3: %f",guidlow, name_id, map->GetId(), x, y, z, ang, rotation0, rotation1, rotation2, rotation3);
        return false;
    }

    Object::_Create(guidlow, goinfo->id, HIGHGUID_GAMEOBJECT);

    m_goInfo = goinfo;

    if (goinfo->type >= MAX_GAMEOBJECT_TYPE)
    {
        sLog.outErrorDb("Gameobject (GUID: %u Entry: %u) not created: it have not exist GO type '%u' in `gameobject_template`. It's will crash client if created.",guidlow,name_id,goinfo->type);
        return false;
    }

    SetFloatValue(GAMEOBJECT_POS_X, x);
    SetFloatValue(GAMEOBJECT_POS_Y, y);
    SetFloatValue(GAMEOBJECT_POS_Z, z);
    SetFloatValue(GAMEOBJECT_FACING, ang);                  //this is not facing angle

    SetFloatValue (GAMEOBJECT_ROTATION, rotation0);
    SetFloatValue (GAMEOBJECT_ROTATION+1, rotation1);
    SetFloatValue (GAMEOBJECT_ROTATION+2, rotation2);
    SetFloatValue (GAMEOBJECT_ROTATION+3, rotation3);

    SetFloatValue(OBJECT_FIELD_SCALE_X, goinfo->size);

    SetUInt32Value(GAMEOBJECT_FACTION, goinfo->faction);
    SetUInt32Value(GAMEOBJECT_FLAGS, goinfo->flags);

    SetUInt32Value(OBJECT_FIELD_ENTRY, goinfo->id);

    SetUInt32Value(GAMEOBJECT_DISPLAYID, goinfo->displayId);

    SetGoState(go_state);
    SetGoType(GameobjectTypes(goinfo->type));

    SetGoAnimProgress(animprogress);

    SetUInt32Value (GAMEOBJECT_ARTKIT, ArtKit);

    // Spell charges for GAMEOBJECT_TYPE_SPELLCASTER (22)
    if (goinfo->type == GAMEOBJECT_TYPE_SPELLCASTER)
        m_charges = goinfo->spellcaster.charges;

    SetZoneScript();

    return true;
}

void GameObject::Update(uint32 update_diff, uint32 p_time)
{
    if (IS_MO_TRANSPORT(GetGUID()))
    {
        //((Transport*)this)->Update(p_time);
        return;
    }

    switch (m_lootState)
    {
        case GO_NOT_READY:
        {
            switch (GetGoType())
            {
                case GAMEOBJECT_TYPE_TRAP:
                {
                    // Arming Time for GAMEOBJECT_TYPE_TRAP (6)
                    Unit* owner = GetOwner();
                    if (owner && ((Player*)owner)->isInCombat())
                        m_cooldownTime = time(NULL) + GetGOInfo()->trap.startDelay;
                    m_lootState = GO_READY;
                    break;
                }
                case GAMEOBJECT_TYPE_FISHINGNODE:
                {
                    // fishing code (bobber ready)
                    if (time(NULL) > m_respawnTime - FISHING_BOBBER_READY_TIME)
                    {
                        // splash bobber (bobber ready now)
                        Unit* caster = GetOwner();
                        if (caster && caster->GetTypeId()==TYPEID_PLAYER)
                        {
                            SetGoState(GO_STATE_ACTIVE);
                            SetUInt32Value(GAMEOBJECT_FLAGS, GO_FLAG_NODESPAWN);

                            UpdateData udata;
                            WorldPacket packet;
                            BuildValuesUpdateBlockForPlayer(&udata,((Player*)caster));
                            udata.BuildPacket(&packet);
                            ((Player*)caster)->GetSession()->SendPacket(&packet);

                            SendGameObjectCustomAnim(GetGUID());
                        }

                        m_lootState = GO_READY;                 // can be successfully open with some chance
                    }
                    return;
                }
                default:
                    m_lootState = GO_READY;                         // for other GOis same switched without delay to GO_READY
                    break;
            }
            // NO BREAK for switch (m_lootState)
        }
        case GO_READY:
        {
            if (m_respawnTime > 0)                          // timer on
            {
                if (m_respawnTime <= time(NULL))            // timer expired
                {
                    m_respawnTime = 0;
                    m_SkillupList.clear();
                    m_usetimes = 0;

                    switch (GetGoType())
                    {
                        case GAMEOBJECT_TYPE_FISHINGNODE:   //  can't fish now
                        {
                            Unit* caster = GetOwner();
                            if (caster && caster->GetTypeId()==TYPEID_PLAYER)
                            {
                                if (caster->m_currentSpells[CURRENT_CHANNELED_SPELL])
                                {
                                    caster->m_currentSpells[CURRENT_CHANNELED_SPELL]->SendChannelUpdate(0);
                                    caster->m_currentSpells[CURRENT_CHANNELED_SPELL]->finish(false);
                                }

                                WorldPacket data(SMSG_FISH_NOT_HOOKED,0);
                                ((Player*)caster)->GetSession()->SendPacket(&data);
                            }
                            // can be delete
                            m_lootState = GO_JUST_DEACTIVATED;
                            return;
                        }
                        case GAMEOBJECT_TYPE_DOOR:
                        case GAMEOBJECT_TYPE_BUTTON:
                            //we need to open doors if they are closed (add there another condition if this code breaks some usage, but it need to be here for battlegrounds)
                            if (GetGoState() != GO_STATE_READY)
                                //SwitchDoorOrButton(false);
                                ResetDoorOrButton();
                            //flags in AB are type_button and we need to add them here so no break!
                        default:
                            if (!m_spawnedByDefault)         // despawn timer
                            {
                                                            // can be despawned or destroyed
                                SetLootState(GO_JUST_DEACTIVATED);
                                return;
                            }
                                                            // respawn timer
                            uint16 poolid = poolhandler.IsPartOfAPool(GetGUIDLow(), TYPEID_GAMEOBJECT);
                            if (poolid)
                                poolhandler.UpdatePool(poolid, GetGUIDLow(), TYPEID_GAMEOBJECT);
                            else
                                GetMap()->Add(this);
                            break;
                    }

                    UpdateObjectVisibility();
                }
            }

            // traps can have time and can not have
            GameObjectInfo const* goInfo = GetGOInfo();
            if (goInfo && goInfo->type == GAMEOBJECT_TYPE_TRAP)
            {
                // traps
                Unit* owner = GetOwner();
                Unit* ok = NULL;                            // pointer to appropriate target if found any

                if (m_cooldownTime >= time(NULL))
                    return;

                bool IsBattleGroundTrap = false;
                //FIXME: this is activation radius (in different casting radius that must be selected from spell data)
                //TODO: move activated state code (cast itself) to GO_ACTIVATED, in this place only check activating and set state
                float radius = (float)(goInfo->trap.radius)/2; // TODO rename radius to diameter (goInfo->trap.radius) should be (goInfo->trap.diameter)

                if (!radius)
                {
                    if (goInfo->trap.cooldown != 3)            // cast in other case (at some triggering/linked go/etc explicit call)
                    {
                        // try to read radius from trap spell
                        if (const SpellEntry *spellEntry = sSpellStore.LookupEntry(goInfo->trap.spellId))
                            radius = GetSpellRadius(spellEntry,0,false);

                        if (!radius)
                            break;
                    }
                    else
                    {
                        if (m_respawnTime > 0)
                            break;

                        radius = goInfo->trap.cooldown;       // battlegrounds gameobjects has data2 == 0 && data5 == 3
                        IsBattleGroundTrap = true;
                    }
                }

                bool NeedDespawn = (goInfo->trap.charges != 0);

                // Note: this hack with search required until GO casting not implemented
                // search unfriendly creature
                if (owner && NeedDespawn)                    // hunter trap
                {
                    Trinity::AnyUnfriendlyNoTotemUnitInObjectRangeCheck u_check(this, owner, radius);
                    Trinity::UnitSearcher<Trinity::AnyUnfriendlyNoTotemUnitInObjectRangeCheck> checker(ok, u_check);

                    Cell::VisitGridObjects(this, checker, radius);

                    if (!ok)
                        Cell::VisitWorldObjects(this, checker, radius);
                }
                else                                        // environmental trap
                {
                    // affect only players
                    Player* p_ok = NULL;
                    Trinity::AnyPlayerInObjectRangeCheck p_check(this, radius);
                    Trinity::PlayerSearcher<Trinity::AnyPlayerInObjectRangeCheck>  checker(p_ok, p_check);

                    Cell::VisitWorldObjects(this,checker, radius);

                    ok = p_ok;
                }

                if (ok)
                {
                    CastSpell(ok, goInfo->trap.spellId);
                    m_cooldownTime = time(NULL) + 4;        // 4 seconds
                    SendCustomAnimation();

                    if (NeedDespawn)
                        SetLootState(GO_JUST_DEACTIVATED);  // can be despawned or destroyed

                    if (IsBattleGroundTrap && ok->GetTypeId() == TYPEID_PLAYER)
                    {
                        //BattleGround gameobjects case
                        if (((Player*)ok)->InBattleGround())
                            if (BattleGround *bg = ((Player*)ok)->GetBattleGround())
                                bg->HandleTriggerBuff(GetGUID());
                    }
                }
            }

            if (m_charges && m_usetimes >= m_charges)
                SetLootState(GO_JUST_DEACTIVATED);          // can be despawned or destroyed

            break;
        }
        case GO_ACTIVATED:
        {
            switch (GetGoType())
            {
                case GAMEOBJECT_TYPE_DOOR:
                case GAMEOBJECT_TYPE_BUTTON:
                    if (GetAutoCloseTime() && (m_cooldownTime < time(NULL)))
                    {
                        ResetDoorOrButton();
                        //SwitchDoorOrButton(false);
                        //SetLootState(GO_JUST_DEACTIVATED);
                    }
                    break;
                case GAMEOBJECT_TYPE_GOOBER:
                    if (GetGOInfo()->goober.consumable && m_cooldownTime < time(NULL))
                    {
                        RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);

                        SetLootState(GO_JUST_DEACTIVATED);
                        m_cooldownTime = 0;
                    }
                    break;
                case GAMEOBJECT_TYPE_CHEST:
                    if (m_groupLootTimer && lootingGroupLeaderGUID)
                    {
                        if (update_diff <= m_groupLootTimer)
                        {
                            m_groupLootTimer -= update_diff;
                        }
                        else
                        {
                            Group* group = objmgr.GetGroupByLeader(lootingGroupLeaderGUID);
                            if (group)
                                group->EndRoll();
                            m_groupLootTimer = 0;
                            lootingGroupLeaderGUID = 0;
                        }
                    }
                    break;
            }
            break;
        }
        case GO_JUST_DEACTIVATED:
        {
            //if Gameobject should cast spell, then this, but some GOs (type = 10) should be destroyed
            if (GetGoType() == GAMEOBJECT_TYPE_GOOBER)
            {
                uint32 spellId = GetGOInfo()->goober.spellId;

                if (spellId)
                {
                    SpellEntry const * spellInfo = sSpellStore.LookupEntry(spellId);

                    std::set<uint32>::iterator it = m_unique_users.begin();
                    std::set<uint32>::iterator end = m_unique_users.end();
                    for (; it != end; it++)
                    {
                        if (Unit* owner = Unit::GetUnit(*this, uint64(*it)))
                        {
                            if (spellInfo)
                                owner->CastSpell(owner, spellId, false);
                            else if(owner->GetTypeId() == TYPEID_PLAYER)
                                HandleNonDbcSpell(spellId, (Player*)owner);
                        }
                    }

                    m_unique_users.clear();
                    m_usetimes = 0;
                }
                //any return here in case battleground traps
            }

            if (GetOwnerGUID())
            {
                if (Unit* owner = GetOwner())
                    owner->RemoveGameObject(this, false);

                m_respawnTime = 0;
                Delete();
                return;
            }

            //burning flags in some battlegrounds, if you find better condition, just add it
            if (GetGoAnimProgress() > 0)
            {
                SendObjectDeSpawnAnim(this->GetGUID());
                //reset flags
                SetUInt32Value(GAMEOBJECT_FLAGS, GetGOInfo()->flags);
            }

            loot.clear();
            SetLootState(GO_READY);

            if (!m_respawnDelayTime)
                return;

            if (!m_spawnedByDefault)
            {
                m_respawnTime = 0;
                return;
            }

            m_respawnTime = time(NULL) + m_respawnDelayTime;

            // if option not set then object will be saved at grid unload
            if (sWorld.getConfig(CONFIG_SAVE_RESPAWN_TIME_IMMEDIATELY))
                SaveRespawnTime();

            UpdateObjectVisibility();

            break;
        }
    }
}

void GameObject::Refresh()
{
    // not refresh despawned not casted GO (despawned casted GO destroyed in all cases anyway)
    if (m_respawnTime > 0 && m_spawnedByDefault)
        return;

    if (isSpawned())
        GetMap()->Add(this);
}

void GameObject::AddUniqueUse(Player* player)
{
    if (m_unique_users.find(player->GetGUIDLow()) != m_unique_users.end())
        return;
    AddUse();
    m_unique_users.insert(player->GetGUIDLow());
}

void GameObject::Delete()
{
    SendObjectDeSpawnAnim(GetGUID());

    SetGoState(GO_STATE_READY);
    SetUInt32Value(GAMEOBJECT_FLAGS, GetGOInfo()->flags);

    uint16 poolid = poolhandler.IsPartOfAPool(GetGUIDLow(), TYPEID_GAMEOBJECT);
    if (poolid)
        poolhandler.UpdatePool(poolid, GetGUIDLow(), TYPEID_GAMEOBJECT);
    else
        AddObjectToRemoveList();
}

void GameObject::getFishLoot(Loot *fishloot)
{
    fishloot->clear();

    uint32 subzone = GetAreaId();

    // if subzone loot exist use it
    if (LootTemplates_Fishing.HaveLootfor (subzone))
        fishloot->FillLoot(subzone, LootTemplates_Fishing, NULL);
    // else use zone loot
    else
        fishloot->FillLoot(GetZoneId(), LootTemplates_Fishing, NULL);
}

void GameObject::SaveToDB()
{
    // this should only be used when the gameobject has already been loaded
    // preferably after adding to map, because mapid may not be valid otherwise
    GameObjectData const *data = objmgr.GetGOData(m_DBTableGuid);
    if (!data)
    {
        sLog.outError("GameObject::SaveToDB failed, cannot get gameobject data!");
        return;
    }

    SaveToDB(GetMapId(), data->spawnMask);
}

void GameObject::SaveToDB(uint32 mapid, uint8 spawnMask)
{
    const GameObjectInfo *goI = GetGOInfo();

    if (!goI)
        return;

    if (!m_DBTableGuid)
        m_DBTableGuid = GetGUIDLow();
    // update in loaded data (changing data only in this place)
    GameObjectData& data = objmgr.NewGOData(m_DBTableGuid);

    // data->guid = guid don't must be update at save
    data.id = GetEntry();
    data.mapid = mapid;
    data.posX = GetFloatValue(GAMEOBJECT_POS_X);
    data.posY = GetFloatValue(GAMEOBJECT_POS_Y);
    data.posZ = GetFloatValue(GAMEOBJECT_POS_Z);
    data.orientation = GetFloatValue(GAMEOBJECT_FACING);
    data.rotation0 = GetFloatValue(GAMEOBJECT_ROTATION+0);
    data.rotation1 = GetFloatValue(GAMEOBJECT_ROTATION+1);
    data.rotation2 = GetFloatValue(GAMEOBJECT_ROTATION+2);
    data.rotation3 = GetFloatValue(GAMEOBJECT_ROTATION+3);
    data.spawntimesecs = m_spawnedByDefault ? m_respawnDelayTime : -(int32)m_respawnDelayTime;
    data.animprogress = GetGoAnimProgress();
    data.go_state = GetGoState();
    data.spawnMask = spawnMask;
    data.ArtKit = GetUInt32Value (GAMEOBJECT_ARTKIT);

    // updated in DB
    static SqlStatementID saveGameObject;
    static SqlStatementID deleteGameObject;

    WorldDatabase.BeginTransaction();

    SqlStatement stmt = WorldDatabase.CreateStatement(deleteGameObject,"DELETE FROM gameobject WHERE guid = ?");
    stmt.PExecute(m_DBTableGuid);

    stmt = WorldDatabase.CreateStatement(saveGameObject,"INSERT INTO gameobject VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    stmt.addUInt64(m_DBTableGuid);
    stmt.addUInt32(GetUInt32Value(OBJECT_FIELD_ENTRY));
    stmt.addUInt32(mapid);
    stmt.addUInt32(spawnMask);
    stmt.addFloat(GetFloatValue(GAMEOBJECT_POS_X));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_POS_Y));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_POS_Z));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_FACING));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_ROTATION));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_ROTATION+1));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_ROTATION+2));
    stmt.addFloat(GetFloatValue(GAMEOBJECT_ROTATION+3));
    stmt.addUInt32(m_respawnDelayTime);
    stmt.addUInt32(GetGoAnimProgress());
    stmt.addUInt32(GetGoState());

    stmt.Execute();
    WorldDatabase.CommitTransaction();
}

bool GameObject::LoadFromDB(uint32 guid, Map *map)
{
    GameObjectData const* data = objmgr.GetGOData(guid);

    if (!data)
    {
        sLog.outErrorDb("ERROR: Gameobject (GUID: %u) not found in table `gameobject`, can't load. ",guid);
        return false;
    }

    uint32 entry = data->id;
    uint32 map_id = data->mapid;
    float x = data->posX;
    float y = data->posY;
    float z = data->posZ;
    float ang = data->orientation;

    float rotation0 = data->rotation0;
    float rotation1 = data->rotation1;
    float rotation2 = data->rotation2;
    float rotation3 = data->rotation3;

    uint32 animprogress = data->animprogress;
    GOState go_state = data->go_state;
    uint32 ArtKit = data->ArtKit;

    m_DBTableGuid = guid;
    if (map->GetInstanceId() != 0)
        guid = objmgr.GenerateLowGuid(HIGHGUID_GAMEOBJECT);

    if (!Create(guid,entry, map, x, y, z, ang, rotation0, rotation1, rotation2, rotation3, animprogress, go_state, ArtKit))
    {
        sLog.outDetail("Couldn't create gameobject with entry: %u", entry);
        return false;
    }

    if (!GetDespawnPossibility())
    {
        SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_NODESPAWN);
        m_spawnedByDefault = true;
        m_respawnDelayTime = 0;
        m_respawnTime = 0;
    }
    else
    {
        if (data->spawntimesecs >= 0)
        {
            m_spawnedByDefault = true;
            m_respawnDelayTime = data->spawntimesecs;
            m_respawnTime = objmgr.GetGORespawnTime(m_DBTableGuid, map->GetInstanceId());

            // ready to respawn
            if (m_respawnTime && m_respawnTime <= time(NULL))
            {
                m_respawnTime = 0;
                objmgr.SaveGORespawnTime(m_DBTableGuid,GetInstanceId(),0);
            }
        }
        else
        {
            m_spawnedByDefault = false;
            m_respawnDelayTime = -data->spawntimesecs;
        }
    }
    m_goData = data;

    return true;
}

void GameObject::DeleteFromDB()
{
    objmgr.SaveGORespawnTime(m_DBTableGuid,GetInstanceId(),0);
    objmgr.DeleteGOData(m_DBTableGuid);
    WorldDatabase.PExecuteLog("DELETE FROM gameobject WHERE guid = '%u'", m_DBTableGuid);
    WorldDatabase.PExecuteLog("DELETE FROM game_event_gameobject WHERE guid = '%u'", m_DBTableGuid);
}

GameObject* GameObject::GetGameObject(WorldObject& object, uint64 guid)
{
    return object.GetMap()->GetGameObject(guid);
}

uint32 GameObject::GetLootId(GameObjectInfo const* ginfo)
{
    if (!ginfo)
        return 0;

    switch (ginfo->type)
    {
        case GAMEOBJECT_TYPE_CHEST:
            return ginfo->chest.lootId;
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            return ginfo->fishinghole.lootId;
        case GAMEOBJECT_TYPE_FISHINGNODE:
            return ginfo->fishnode.lootId;
        default:
            return 0;
    }
}

/*********************************************************/
/***                    QUEST SYSTEM                   ***/
/*********************************************************/
bool GameObject::hasQuest(uint32 quest_id) const
{
    QuestRelations const& qr = objmgr.mGOQuestRelations;
    for (QuestRelations::const_iterator itr = qr.lower_bound(GetEntry()); itr != qr.upper_bound(GetEntry()); ++itr)
    {
        if (itr->second==quest_id)
            return true;
    }
    return false;
}

bool GameObject::hasInvolvedQuest(uint32 quest_id) const
{
    QuestRelations const& qr = objmgr.mGOQuestInvolvedRelations;
    for (QuestRelations::const_iterator itr = qr.lower_bound(GetEntry()); itr != qr.upper_bound(GetEntry()); ++itr)
    {
        if (itr->second==quest_id)
            return true;
    }
    return false;
}

bool GameObject::IsTransport() const
{
    // If something is marked as a transport, don't transmit an out of range packet for it.
    GameObjectInfo const * gInfo = GetGOInfo();
    if (!gInfo) return false;
    return gInfo->type == GAMEOBJECT_TYPE_TRANSPORT || gInfo->type == GAMEOBJECT_TYPE_MO_TRANSPORT;
}

Unit* GameObject::GetOwner() const
{
    return GetMap()->GetUnit(GetOwnerGUID());
}

void GameObject::SaveRespawnTime()
{
    if (m_goData && m_goData->dbData && m_respawnTime > time(NULL) && m_spawnedByDefault)
        objmgr.SaveGORespawnTime(m_DBTableGuid,GetInstanceId(),m_respawnTime);
}

bool GameObject::isVisibleForInState(Player const* u, bool inVisibleList) const
{
    // Not in world
    if (!IsInWorld() || !u->IsInWorld())
        return false;

    // Transport always visible at this step implementation
    if (IsTransport() && IsInMap(u))
        return true;

    // quick check visibility false cases for non-GM-mode
    if (!u->isGameMaster())
    {
        // despawned and then not visible for non-GM in GM-mode
        if (!isSpawned())
            return false;

        // special invisibility cases
        if (GetGOInfo()->type == GAMEOBJECT_TYPE_TRAP && GetGOInfo()->trap.stealthed)
        {
            Unit *owner = GetOwner();
            if (owner && u->IsHostileTo(owner) && !canDetectTrap(u, GetDistance(u)))
                return false;
        }

        // Smuggled Mana Cell required 10 invisibility type detection/state
        if (GetEntry()==187039 && ((u->m_detectInvisibilityMask | u->m_invisibilityMask) & (1<<10))==0)
            return false;
    }

    // check distance
    const WorldObject* viewPoint = u->GetFarsightTarget();
    if (!viewPoint || !u->HasFarsightVision()) viewPoint = u;

    return IsWithinDistInMap(viewPoint, World::GetMaxVisibleDistanceForObject() + (inVisibleList ? World::GetVisibleObjectGreyDistance() : 0.0f), false);
}

bool GameObject::canDetectTrap(Player const* u, float distance) const
{
    if (u->hasUnitState(UNIT_STAT_STUNNED))
        return false;
    if (distance < GetGOInfo()->size) //collision
        return true;
    if (!u->HasInArc(M_PI, this)) //behind
        return false;
    if (u->HasAuraType(SPELL_AURA_DETECT_STEALTH))
        return true;

    //Visible distance is modified by -Level Diff (every level diff = 0.25f in visible distance)
    float visibleDistance = (int32(u->getLevel()) - int32(GetOwner()->getLevel()))* 0.25f;
    //GetModifier for trap (miscvalue 1)
    //35y for aura 2836
    //WARNING: these values are guessed, may be not blizzlike
    visibleDistance += u->GetTotalAuraModifierByMiscValue(SPELL_AURA_MOD_DETECT, 1)* 0.5f;

    return distance < visibleDistance;
}

void GameObject::Respawn()
{
    if (m_spawnedByDefault && m_respawnTime > 0)
    {
        m_respawnTime = time(NULL);
        objmgr.SaveGORespawnTime(m_DBTableGuid,GetInstanceId(),0);
    }
}

bool GameObject::ActivateToQuest(Player *pTarget)const
{
    if (!objmgr.IsGameObjectForQuests(GetEntry()))
        return false;

    switch (GetGoType())
    {
        // scan GO chest with loot including quest items
        case GAMEOBJECT_TYPE_CHEST:
        {
            if (LootTemplates_Gameobject.HaveQuestLootForPlayer(GetLootId(), pTarget))
            {
                //TODO: fix this hack
                //look for battlegroundAV for some objects which are only activated after mine gots captured by own team
                if (GetEntry() == BG_AV_OBJECTID_MINE_N || GetEntry() == BG_AV_OBJECTID_MINE_S)
                    if (BattleGround *bg = pTarget->GetBattleGround())
                        if (bg->GetTypeID() == BATTLEGROUND_AV && !(((BattleGroundAV*)bg)->PlayerCanDoMineQuest(GetEntry(),pTarget->GetTeam())))
                            return false;
                return true;
            }
            break;
        }
        case GAMEOBJECT_TYPE_GOOBER:
        {
            if (pTarget->GetQuestStatus(GetGOInfo()->goober.questId) == QUEST_STATUS_INCOMPLETE)
                return true;
            break;
        }
        default:
            break;
    }

    return false;
}

void GameObject::TriggeringLinkedGameObject(uint32 trapEntry, Unit* target)
{
    GameObjectInfo const* trapInfo = sGOStorage.LookupEntry<GameObjectInfo>(trapEntry);
    if (!trapInfo || trapInfo->type!=GAMEOBJECT_TYPE_TRAP)
        return;

    SpellEntry const* trapSpell = sSpellStore.LookupEntry(trapInfo->trap.spellId);
    if (!trapSpell)                                          // checked at load already
        return;

    float range = GetSpellMaxRange(sSpellRangeStore.LookupEntry(trapSpell->rangeIndex));

    // search nearest linked GO
    GameObject* trapGO = NULL;
    {
        // using original GO distance
        Trinity::NearestGameObjectEntryInObjectRangeCheck go_check(*target, trapEntry, range);
        Trinity::GameObjectLastSearcher<Trinity::NearestGameObjectEntryInObjectRangeCheck> checker(trapGO, go_check);

        Cell::VisitGridObjects(this, checker, range);
    }

    // found correct GO
    // FIXME: when GO casting will be implemented trap must cast spell to target
    if (trapGO)
        target->CastSpell(target, trapSpell ,true);
}

GameObject* GameObject::LookupFishingHoleAround(float range)
{
    GameObject* ok = NULL;

    Trinity::NearestGameObjectFishingHole u_check(*this, range);
    Trinity::GameObjectSearcher<Trinity::NearestGameObjectFishingHole> checker(ok, u_check);

    Cell::VisitGridObjects(this, checker, range);
    return ok;
}

void GameObject::ResetDoorOrButton()
{
    if (m_lootState == GO_READY || m_lootState == GO_JUST_DEACTIVATED)
        return;

    SwitchDoorOrButton(false);
    SetLootState(GO_JUST_DEACTIVATED);
    m_cooldownTime = 0;
}

void GameObject::UseDoorOrButton(uint32 time_to_restore, bool alternative /* = false */)
{
    if (m_lootState != GO_READY)
        return;

    if (!time_to_restore)
        time_to_restore = GetAutoCloseTime();

    SwitchDoorOrButton(true, alternative);
    SetLootState(GO_ACTIVATED);

    m_cooldownTime = time(NULL) + time_to_restore;
}

void GameObject::SetGoArtKit(uint32 kit)
{
    SetUInt32Value(GAMEOBJECT_ARTKIT, kit);
    GameObjectData *data = const_cast<GameObjectData*>(objmgr.GetGOData(m_DBTableGuid));
    if (data)
        data->ArtKit = kit;
}

void GameObject::SwitchDoorOrButton(bool activate, bool alternative /* = false */)
{
    if (activate)
        SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
    else
        RemoveFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);

    if (GetGoState() == GO_STATE_READY)                     //if closed -> open
        SetGoState(alternative ? GO_STATE_ACTIVE_ALTERNATIVE : GO_STATE_ACTIVE);
    else                                                    //if open -> close
        SetGoState(GO_STATE_READY);
}

void GameObject::Use(Unit* user)
{
    // by default spell caster is user
    Unit* spellCaster = user;
    uint32 spellId = 0;

    switch (GetGoType())
    {
        case GAMEOBJECT_TYPE_DOOR:                          //0
        case GAMEOBJECT_TYPE_BUTTON:                        //1
            //doors/buttons never really despawn, only reset to default state/flags
            UseDoorOrButton();

            // activate script
            GetMap()->ScriptsStart(sGameObjectScripts, GetDBTableGUIDLow(), spellCaster, this);
            return;

        case GAMEOBJECT_TYPE_QUESTGIVER:                    //2
        {
            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            player->PrepareQuestMenu(GetGUID());
            player->SendPreparedQuest(GetGUID());
            return;
        }
        //Sitting: Wooden bench, chairs enzz
        case GAMEOBJECT_TYPE_CHAIR:                         //7
        {
            GameObjectInfo const* info = GetGOInfo();
            if (!info)
                return;

            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            // a chair may have n slots. we have to calculate their positions and teleport the player to the nearest one

            // check if the db is sane
            if (info->chair.slots > 0)
            {
                float lowestDist = DEFAULT_VISIBILITY_DISTANCE;

                float x_lowest = GetPositionX();
                float y_lowest = GetPositionY();

                // the object orientation + 1/2 pi
                // every slot will be on that straight line
                float orthogonalOrientation = GetOrientation()+M_PI*0.5f;
                // find nearest slot
                for (uint32 i=0; i<info->chair.slots; i++)
                {
                    // the distance between this slot and the center of the go - imagine a 1D space
                    float relativeDistance = (info->size*i)-(info->size*(info->chair.slots-1)/2.0f);

                    float x_i = GetPositionX() + relativeDistance * cos(orthogonalOrientation);
                    float y_i = GetPositionY() + relativeDistance * sin(orthogonalOrientation);

                    // calculate the distance between the player and this slot
                    float thisDistance = player->GetDistance2d(x_i, y_i);

                    /* debug code. It will spawn a npc on each slot to visualize them.
                    Creature* helper = player->SummonCreature(14496, x_i, y_i, GetPositionZ(), GetOrientation(), TEMPSUMMON_TIMED_OR_DEAD_DESPAWN, 10000);
                    std::ostringstream output;
                    output << i << ": thisDist: " << thisDistance;
                    helper->MonsterSay(output.str().c_str(), LANG_UNIVERSAL, 0);
                    */

                    if (thisDistance <= lowestDist)
                    {
                        lowestDist = thisDistance;
                        x_lowest = x_i;
                        y_lowest = y_i;
                    }
                }
                player->TeleportTo(GetMapId(), x_lowest, y_lowest, GetPositionZ(), GetOrientation(),TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET);
            }
            else
            {
                // fallback, will always work
                player->TeleportTo(GetMapId(), GetPositionX(), GetPositionY(), GetPositionZ(), GetOrientation(),TELE_TO_NOT_LEAVE_TRANSPORT | TELE_TO_NOT_LEAVE_COMBAT | TELE_TO_NOT_UNSUMMON_PET);
            }
            player->SetStandState(PLAYER_STATE_SIT_LOW_CHAIR+info->chair.height);
            return;
        }
        case GAMEOBJECT_TYPE_GOOBER:                        //10
        {
            GameObjectInfo const* info = GetGOInfo();

            if (user->GetTypeId() == TYPEID_PLAYER)
            {
                Player* player = (Player*)user;

                // show page
                if (info->goober.pageId)
                {
                    WorldPacket data(SMSG_GAMEOBJECT_PAGETEXT, 8);
                    data << GetGUID();
                    player->GetSession()->SendPacket(&data);
                }

                // possible quest objective for active quests
                if (info->goober.questId && objmgr.GetQuestTemplate(info->goober.questId))
                {
                    //Quest require to be active for GO using
                    if (player->GetQuestStatus(info->goober.questId) != QUEST_STATUS_INCOMPLETE)
                        break;
                }

                player->CastedCreatureOrGO(GetEntry(), GetGUID(), 0);
            }

            if (uint32 trapEntry = info->goober.linkedTrapId)
                TriggeringLinkedGameObject(trapEntry, user);

            /*
            SetFlag(GAMEOBJECT_FLAGS, GO_FLAG_IN_USE);
            SetLootState(GO_ACTIVATED);

            uint32 time_to_restore = GetAutoCloseTime();

            // this appear to be ok, however others exist in addition to this that should have custom (ex: 190510, 188692, 187389)
            if (time_to_restore && info->goober.customAnim)
            {
                WorldPacket data(SMSG_GAMEOBJECT_CUSTOM_ANIM, 8+4);
                data << uint64(GetGUID());
                data << uint32(0);                                      // not known what this is
                SendMessageToSet(&data, true);
            }
            else
                SetGoState(GO_STATE_ACTIVE);

            m_cooldownTime = time(NULL) + time_to_restore;
            */
            // cast this spell later if provided
            spellId = info->goober.spellId;
            break;
        }
        case GAMEOBJECT_TYPE_CAMERA:                        //13
        {
            GameObjectInfo const* info = GetGOInfo();
            if (!info)
                return;

            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            if (info->camera.cinematicId)
            {
                WorldPacket data(SMSG_TRIGGER_CINEMATIC, 4);
                data << info->camera.cinematicId;
                player->GetSession()->SendPacket(&data);
            }
            return;
        }
        //fishing bobber
        case GAMEOBJECT_TYPE_FISHINGNODE:                   //17
        {
            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            if (player->GetGUID() != GetOwnerGUID())
                return;

            switch (getLootState())
            {
                case GO_READY:                              // ready for loot
                {
                    // 1) skill must be >= base_zone_skill
                    // 2) if skill == base_zone_skill => 5% chance
                    // 3) chance is linear dependence from (base_zone_skill-skill)

                    uint32 subzone = GetAreaId();

                    int32 zone_skill = objmgr.GetFishingBaseSkillLevel(subzone);
                    if (!zone_skill)
                        zone_skill = objmgr.GetFishingBaseSkillLevel(GetZoneId());

                    //provide error, no fishable zone or area should be 0
                    if (!zone_skill)
                        sLog.outErrorDb("Fishable areaId %u are not properly defined in `skill_fishing_base_level`.",subzone);

                    int32 skill = player->GetSkillValue(SKILL_FISHING);
                    int32 chance = skill - zone_skill + 5;
                    int32 roll = GetMap()->irand(1,100);

                    DEBUG_LOG("Fishing check (skill: %i zone min skill: %i chance %i roll: %i",skill,zone_skill,chance,roll);

                    if (skill >= zone_skill && chance >= roll)
                    {
                        // prevent removing GO at spell cancel
                        player->RemoveGameObject(this,false);
                        SetOwnerGUID(player->GetGUID());

                        //fish catched
                        player->UpdateFishingSkill();

                        //TODO: find reasonable value for fishing hole search
                        GameObject* ok = LookupFishingHoleAround(20.0f + CONTACT_DISTANCE);
                        if (ok)
                        {
                            player->SendLoot(ok->GetGUID(),LOOT_FISHINGHOLE);
                            SetLootState(GO_JUST_DEACTIVATED);
                        }
                        else
                            player->SendLoot(GetGUID(),LOOT_FISHING);
                    }
                    else
                    {
                        // fish escaped, can be deleted now
                        SetLootState(GO_JUST_DEACTIVATED);

                        WorldPacket data(SMSG_FISH_ESCAPED, 0);
                        player->GetSession()->SendPacket(&data);
                    }
                    break;
                }
                case GO_JUST_DEACTIVATED:                   // nothing to do, will be deleted at next update
                    break;
                default:
                {
                    SetLootState(GO_JUST_DEACTIVATED);

                    WorldPacket data(SMSG_FISH_NOT_HOOKED, 0);
                    player->GetSession()->SendPacket(&data);
                    break;
                }
            }

            if (player->m_currentSpells[CURRENT_CHANNELED_SPELL])
            {
                player->m_currentSpells[CURRENT_CHANNELED_SPELL]->SendChannelUpdate(0);
                player->m_currentSpells[CURRENT_CHANNELED_SPELL]->finish();
            }
            return;
        }

        case GAMEOBJECT_TYPE_SUMMONING_RITUAL:              //18
        {
            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            Unit* caster = GetOwner();

            GameObjectInfo const* info = GetGOInfo();

            if (!caster || caster->GetTypeId()!=TYPEID_PLAYER)
                return;

            // accept only use by player from same group for caster except caster itself
            if (((Player*)caster)==player || !((Player*)caster)->IsInSameRaidWith(player))
                return;

            AddUniqueUse(player);

            // full amount unique participants including original summoner
            if (GetUniqueUseCount() < info->summoningRitual.reqParticipants)
                return;

            // in case summoning ritual caster is GO creator
            spellCaster = caster;

            if (!caster->m_currentSpells[CURRENT_CHANNELED_SPELL])
                return;

            spellId = info->summoningRitual.spellId;

            // finish spell
            caster->m_currentSpells[CURRENT_CHANNELED_SPELL]->SendChannelUpdate(0);
            caster->m_currentSpells[CURRENT_CHANNELED_SPELL]->finish();

            // can be deleted now
            SetLootState(GO_JUST_DEACTIVATED);

            // go to end function to spell casting
            break;
        }
        case GAMEOBJECT_TYPE_SPELLCASTER:                   //22
        {
            SetUInt32Value(GAMEOBJECT_FLAGS,2);

            GameObjectInfo const* info = GetGOInfo();
            if (!info)
                return;

            if (info->spellcaster.partyOnly)
            {
                Unit* caster = GetOwner();
                if (!caster || caster->GetTypeId()!=TYPEID_PLAYER)
                    return;

                if (user->GetTypeId()!=TYPEID_PLAYER || !((Player*)user)->IsInSameRaidWith((Player*)caster))
                    return;
            }

            spellId = info->spellcaster.spellId;

            AddUse();
            break;
        }
        case GAMEOBJECT_TYPE_MEETINGSTONE:                  //23
        {
            GameObjectInfo const* info = GetGOInfo();

            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            Player* targetPlayer = ObjectAccessor::FindPlayer(player->GetSelection());

            // accept only use by player from same group for caster except caster itself
            if (!targetPlayer || targetPlayer == player || !targetPlayer->IsInSameGroupWith(player))
                return;

            //required lvl checks!
            uint8 level = player->getLevel();
            if (level < info->meetingstone.minLevel || level > info->meetingstone.maxLevel)
                return;
            level = targetPlayer->getLevel();
            if (level < info->meetingstone.minLevel || level > info->meetingstone.maxLevel)
                return;

            spellId = 23598;

            break;
        }

        case GAMEOBJECT_TYPE_FLAGSTAND:                     // 24
        {
            if (user->GetTypeId() != TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            if (player->isAllowUseBattleGroundObject())
            {
                // in battleground check
                BattleGround *bg = player->GetBattleGround();
                if (!bg)
                    return;

                 bg->EventPlayerClickedOnFlag(player, this);
                 return;    //we don;t need to delete flag ... it is despawned!
            }
            break;
        }
        case GAMEOBJECT_TYPE_FLAGDROP:                      // 26
        {
            if (user->GetTypeId()!=TYPEID_PLAYER)
                return;

            Player* player = (Player*)user;

            if (player->isAllowUseBattleGroundObject())
            {
                // in battleground check
                BattleGround *bg = player->GetBattleGround();
                if (!bg)
                    return;
                // BG flag dropped
                // WS:
                // 179785 - Silverwing Flag
                // 179786 - Warsong Flag
                // EotS:
                // 184142 - Netherstorm Flag
                GameObjectInfo const* info = GetGOInfo();
                if (info)
                {
                    switch (info->id)
                    {
                        case 179785:                        // Silverwing Flag
                            // check if it's correct bg
                            if (bg->GetTypeID() == BATTLEGROUND_WS)
                                bg->EventPlayerClickedOnFlag(player, this);
                            break;
                        case 179786:                        // Warsong Flag
                            if (bg->GetTypeID() == BATTLEGROUND_WS)
                                bg->EventPlayerClickedOnFlag(player, this);
                            break;
                        case 184142:                        // Netherstorm Flag
                            if (bg->GetTypeID() == BATTLEGROUND_EY)
                                bg->EventPlayerClickedOnFlag(player, this);
                            break;
                    }
                }
                //this cause to call return, all flags must be deleted here!!
                spellId = 0;
                Delete();
            }
            break;
        }
        default:
            sLog.outDebug("Unknown Object Type %u", GetGoType());
            break;
    }

    if (!spellId)
        return;

    SpellEntry const *spellInfo = sSpellStore.LookupEntry(spellId);
    if (!spellInfo)
    {
        if (user->GetTypeId() != TYPEID_PLAYER || sOutdoorPvPMgr.HandleCustomSpell((Player*)user,spellId,this))
            return;

        HandleNonDbcSpell(spellId, (Player*)user);
        return;
    }

    Spell *spell = new Spell(spellCaster, spellInfo, false);

    // spell target is user of GO
    SpellCastTargets targets;
    targets.setUnitTarget(user);

    spell->prepare(&targets);
}

void GameObject::CastSpell(Unit* target, uint32 spell)
{
    //summon world trigger
    Creature *trigger = SummonTrigger(GetPositionX(), GetPositionY(), GetPositionZ(), 0, 1);
    if (!trigger) return;

    trigger->SetVisibility(VISIBILITY_OFF); //should this be true?
    if (Unit *owner = GetOwner())
    {
        trigger->setFaction(owner->getFaction());
        trigger->CastSpell(target, spell, true, 0, 0, owner->GetGUID());
    }
    else
    {
        trigger->setFaction(14);
        trigger->CastSpell(target, spell, true, 0, 0, target->GetGUID());
    }
    //trigger->setDeathState(JUST_DIED);
    //trigger->RemoveCorpse();
}

void GameObject::CastSpell(GameObject* target, uint32 spell)
{
    //summon world trigger
    Creature *trigger = SummonTrigger(GetPositionX(), GetPositionY(), GetPositionZ(), 0, 1);
    if (!trigger) return;

    trigger->SetVisibility(VISIBILITY_OFF); //should this be true?
    if (Unit *owner = GetOwner())
    {
        trigger->setFaction(owner->getFaction());
        trigger->CastSpell(target, spell, true, 0, 0, owner->GetGUID());
    }
    else
    {
        trigger->setFaction(14);
        trigger->CastSpell(target, spell, true, 0, 0, target->GetGUID());
    }
}

// overwrite WorldObject function for proper name localization
const char* GameObject::GetNameForLocaleIdx(int32 loc_idx) const
{
    if (loc_idx >= 0)
    {
        GameObjectLocale const *cl = objmgr.GetGameObjectLocale(GetEntry());
        if (cl)
        {
            if (cl->Name.size() > loc_idx && !cl->Name[loc_idx].empty())
                return cl->Name[loc_idx].c_str();
        }
    }

    return GetName();
}

float GameObject::GetObjectBoundingRadius() const
{
    //FIXME:
    // 1. This is clearly hack way because GameObjectDisplayInfoEntry have 6 floats related to GO sizes, but better that use DEFAULT_WORLD_OBJECT_SIZE
    // 2. In some cases this must be only interactive size, not GO size, current way can affect creature target point auto-selection in strange ways for big underground/virtual GOs
    if (GameObjectDisplayInfoEntry const* dispEntry = sGameObjectDisplayInfoStore.LookupEntry(GetGOInfo()->displayId))
        return fabs(dispEntry->unknown12) * GetGOInfo()->size;

    return DEFAULT_WORLD_OBJECT_SIZE;
}

void GameObject::HandleNonDbcSpell(uint32 spellId, Player* pUser)
{
    switch(spellId)
    {
        case 37639: // Nether Drake Egg
        case 37264: // Power Converter
        {
            uint32 entry = spellId == 37639 ? 20021 : 21729;

            float x, y, z;
            pUser->GetClosePoint(x, y, z, 0.0f, 3.0f, frand(0, 2*M_PI));
            if (Creature *pSummon = pUser->SummonCreature(entry, x, y, z, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5000))
                pSummon->AI()->AttackStart(pUser);

            break;
        }

        default:
            sLog.outDebug("Gameobject: %s, %u type: %u. casted non-handled and non-existing spell: %u", GetName(), GetEntry(), GetGoType(), spellId);
            break;
    }
}
