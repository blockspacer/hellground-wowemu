/* Copyright (C) 2006 - 2008 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
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

/* ScriptData
SDName: Instance_Karazhan
SD%Complete: 70
SDComment: Instance Script for Karazhan to help in various encounters. TODO: GameObject visibility for Opera event.
SDCategory: Karazhan
EndScriptData */

#include "precompiled.h"
#include "instance_karazhan.h"

instance_karazhan::instance_karazhan(Map* map) : ScriptedInstance(map) {Initialize();}

void instance_karazhan::Initialize()
{
    for (uint8 i = 0; i < ENCOUNTERS; ++i)
        Encounters[i] = NOT_STARTED;

    OperaEvent          = urand(1,3);                   // 1 - OZ, 2 - HOOD, 3 - RAJ, this never gets altered.
    OzDeathCount        = 0;

    CurtainGUID         = 0;
    StageDoorLeftGUID   = 0;
    StageDoorRightGUID  = 0;

    KilrekGUID          = 0;
    TerestianGUID       = 0;
    MoroesGUID          = 0;
    AranGUID            = 0;

    NightbaneGUID       = 0;

    LibraryDoor         = 0;
    MassiveDoor         = 0;
    GamesmansDoor       = 0;
    GamesmansExitDoor   = 0;
    NetherspaceDoor     = 0;
    MastersTerraceDoor[0]= 0;
    MastersTerraceDoor[1]= 0;
    ImageGUID           = 0;
    MedivhGUID          = 0;
    CheckTimer          = 5000;

    needRespawn         = true;
}

bool instance_karazhan::IsEncounterInProgress() const
{
    for (uint8 i = 0; i < ENCOUNTERS; ++i)
        if (Encounters[i] != DONE && Encounters[i] != NOT_STARTED)
            return true;

    return false;
}

uint32 instance_karazhan::GetData(uint32 identifier)
{
    switch (identifier)
    {
        case DATA_ATTUMEN_EVENT:          return Encounters[0];
        case DATA_MOROES_EVENT:           return Encounters[1];
        case DATA_MAIDENOFVIRTUE_EVENT:   return Encounters[2];
        case DATA_OPTIONAL_BOSS_EVENT:    return Encounters[3];
        case DATA_OPERA_EVENT:            return Encounters[4];
        case DATA_CURATOR_EVENT:          return Encounters[5];
        case DATA_SHADEOFARAN_EVENT:      return Encounters[6];
        case DATA_TERESTIAN_EVENT:        return Encounters[7];
        case DATA_NETHERSPITE_EVENT:      return Encounters[8];
        case DATA_CHESS_EVENT:            return Encounters[9];
        case DATA_MALCHEZZAR_EVENT:       return Encounters[10];
        case DATA_NIGHTBANE_EVENT:        return Encounters[11];
        case CHESS_EVENT_TEAM:            return Encounters[12];
        case DATA_OPERA_PERFORMANCE:      return OperaEvent;
        case DATA_OPERA_OZ_DEATHCOUNT:    return OzDeathCount;
        case DATA_IMAGE_OF_MEDIVH:        return ImageGUID;
    }

    return 0;
}

void instance_karazhan::OnCreatureCreate(Creature *creature, uint32 entry)
{
    uint64 temp;
    switch (creature->GetEntry())
    {
        case 17229:   KilrekGUID = creature->GetGUID();      break;
        case 15688:   TerestianGUID = creature->GetGUID();   break;
        case 15687:   MoroesGUID = creature->GetGUID();      break;
        case 16524:   AranGUID = creature->GetGUID();        break;
        case 16816:   MedivhGUID = creature->GetGUID();      break;
		case 22519:
		case 17469:
		case 17211:
		case 21748:
		case 21664:
		case 21750:
		case 21683:
		case 21747:
		case 21682:
		case 21726:
		case 21160:
		case 21752:
		case 21684:
			temp = creature->GetGUID();
			forChessList.push_back(temp);
			break;
    }
}

uint64 instance_karazhan::GetData64(uint32 data)
{
    switch (data)
    {
        case DATA_KILREK:                      return KilrekGUID;
        case DATA_TERESTIAN:                   return TerestianGUID;
        case DATA_MOROES:                      return MoroesGUID;
        case DATA_NIGHTBANE:                   return NightbaneGUID;
        case DATA_GAMEOBJECT_STAGEDOORLEFT:    return StageDoorLeftGUID;
        case DATA_GAMEOBJECT_STAGEDOORRIGHT:   return StageDoorRightGUID;
        case DATA_GAMEOBJECT_CURTAINS:         return CurtainGUID;
        case DATA_GAMEOBJECT_LIBRARY_DOOR:     return LibraryDoor;
        case DATA_GAMEOBJECT_MASSIVE_DOOR:     return MassiveDoor;
        case DATA_GAMEOBJECT_GAME_DOOR:        return GamesmansDoor;
        case DATA_GAMEOBJECT_GAME_EXIT_DOOR:   return GamesmansExitDoor;
        case DATA_GAMEOBJECT_NETHER_DOOR:      return NetherspaceDoor;
        case DATA_MASTERS_TERRACE_DOOR_1:      return NetherspaceDoor;
        case DATA_MASTERS_TERRACE_DOOR_2:      return MastersTerraceDoor[1];
        case DATA_ARAN:                        return AranGUID;
        case DATA_CHESS_ECHO_OF_MEDIVH:        return MedivhGUID;
    }

    return 0;
}

void instance_karazhan::SetData(uint32 type, uint32 data)
{
    switch (type)
    {
    case DATA_ATTUMEN_EVENT:
        if(Encounters[0] != DONE)
            Encounters[0] = data;
        break;
    case DATA_MOROES_EVENT:
        if(Encounters[1] != DONE)
            Encounters[1] = data;
        break;
    case DATA_MAIDENOFVIRTUE_EVENT:
        if(Encounters[2] != DONE)
            Encounters[2] = data;
        break;
    case DATA_OPTIONAL_BOSS_EVENT:
        if(Encounters[3] != DONE)
            Encounters[3] = data;
        break;
    case DATA_OPERA_EVENT:
        if(Encounters[4] != DONE)
            Encounters[4] = data;
        break;
    case DATA_CURATOR_EVENT:
        if(Encounters[5] != DONE)
            Encounters[5] = data;
        break;
    case DATA_SHADEOFARAN_EVENT:
        if(Encounters[6] != DONE)
            Encounters[6] = data;
        break;
    case DATA_TERESTIAN_EVENT:
        if(Encounters[7] != DONE)
            Encounters[7] = data;
        break;
    case DATA_NETHERSPITE_EVENT:
        if(Encounters[8] != DONE)
            Encounters[8] = data;
        break;
    case DATA_CHESS_EVENT:
        if(Encounters[9] != DONE)
            Encounters[9] = data;
        break;
    case CHESS_EVENT_TEAM:
        if(Encounters[12] != DONE)
            Encounters[12] = data;
        break;
    case DATA_MALCHEZZAR_EVENT:
        if(Encounters[10] != DONE)
            Encounters[10] = data;
        break;
    case DATA_NIGHTBANE_EVENT:
        if(Encounters[1] != DONE)
            Encounters[1] = data;
        break;
    case DATA_OPERA_OZ_DEATHCOUNT:
        ++OzDeathCount;
        break;
    }

    if(data == DONE)
        SaveToDB();
}

void instance_karazhan::SetData64(uint32 identifier, uint64 data)
{
    switch(identifier)
    {
    case DATA_IMAGE_OF_MEDIVH:
        ImageGUID = data;
        break;
    case DATA_NIGHTBANE:
        NightbaneGUID = data;
        break;
    default:
        break;
    }
}

void instance_karazhan::OnObjectCreate(GameObject* go)
{
    switch(go->GetEntry())
    {
    case 183932:
        CurtainGUID           = go->GetGUID();
        break;
    case 184278:
        StageDoorLeftGUID     = go->GetGUID();
        break;
    case 184279:
        StageDoorRightGUID    = go->GetGUID();
        break;
    case 184517:
        LibraryDoor           = go->GetGUID();
        break;
    case 185521:
        MassiveDoor           = go->GetGUID();
        break;
    case 184276:
        GamesmansDoor         = go->GetGUID();
        break;
    case 184277:
        GamesmansExitDoor     = go->GetGUID();
        break;
    case 185134:
        NetherspaceDoor       = go->GetGUID();
        break;
    case 184274:
        MastersTerraceDoor[0] = go->GetGUID();
        break;
    case 184280:
        MastersTerraceDoor[1] = go->GetGUID();
        break;
    }

    switch(OperaEvent)
    {
    //TODO: Set Object visibilities for Opera based on performance
    case EVENT_OZ:
        break;
    case EVENT_HOOD:
        break;
    case EVENT_RAJ:
        break;
    }
}

const char* instance_karazhan::Save()
{
    OUT_SAVE_INST_DATA;
    std::ostringstream stream;
    stream << Encounters[0] << " "  << Encounters[1] << " "  << Encounters[2] << " "  << Encounters[3] << " "
        << Encounters[4] << " "  << Encounters[5] << " "  << Encounters[6] << " "  << Encounters[7] << " "
        << Encounters[8] << " "  << Encounters[9] << " "  << Encounters[10] << " "  << Encounters[11];
    char* out = new char[stream.str().length() + 1];
    strcpy(out, stream.str().c_str());
    if(out)
    {
        OUT_SAVE_INST_DATA_COMPLETE;
        return out;
    }

    return NULL;
}

void instance_karazhan::Load(const char* in)
{
    if(!in)
    {
        OUT_LOAD_INST_DATA_FAIL;
        return;
    }

    OUT_LOAD_INST_DATA(in);
    std::istringstream stream(in);
    stream >> Encounters[0] >> Encounters[1] >> Encounters[2] >> Encounters[3]
        >> Encounters[4] >> Encounters[5] >> Encounters[6] >> Encounters[7]
        >> Encounters[8] >> Encounters[9] >> Encounters[10] >> Encounters[11];
    for(uint8 i = 0; i < ENCOUNTERS; ++i)
        if(Encounters[i] == IN_PROGRESS)                // Do not load an encounter as "In Progress" - reset it instead.
            Encounters[i] = NOT_STARTED;
    OUT_LOAD_INST_DATA_COMPLETE;
}

void instance_karazhan::Update(uint32 diff)
{
    if(GetData(DATA_TERESTIAN_EVENT) == IN_PROGRESS)
    {
        if(CheckTimer < diff)
        {
            Creature *Kilrek = instance->GetCreature(KilrekGUID);
            if(Kilrek && needRespawn)
            {
                Kilrek->Respawn();
                needRespawn = false;

                Creature *Terestian = instance->GetCreature(TerestianGUID);
                if(Terestian && Terestian->isAlive())
                    Terestian->RemoveAurasDueToSpell(SPELL_BROKEN_PACT);
            }

            if(Kilrek && !Kilrek->isAlive() && !needRespawn)
            {
                needRespawn = true;
                CheckTimer = 45000;
            }
            else
                CheckTimer = 5000;
        }
        else
            CheckTimer -= diff;
    }
}

InstanceData* GetInstanceData_instance_karazhan(Map* map)
{
    return new instance_karazhan(map);
}

void AddSC_instance_karazhan()
{
    Script *newscript;
    newscript = new Script;
    newscript->Name = "instance_karazhan";
    newscript->GetInstanceData = &GetInstanceData_instance_karazhan;
    newscript->RegisterSelf();
}

