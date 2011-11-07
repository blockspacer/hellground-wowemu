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
SDName: Instance_Magisters_Terrace
SD%Complete: 90
SDComment: Make Kael trash pack, final debugging
SDCategory: Magister's Terrace
EndScriptData */

#include "precompiled.h"
#include "def_magisters_terrace.h"

#define ENCOUNTERS      4

// TODO
#define TRASH_Z         -14.38
float KaelTrashLocations[6][2]=
{
};
uint32 TrashPackEntry[8] = 
{
    24683,  // mob_sunwell_mage_guard
    24685,  // mob_sunblade_magister
    24686,  // mob_sunblade_warlock
    24687,  // mob_sunblade_physician
    24684,  // mob_sunblade_blood_knight
    24697,  // mob_sister_of_torment
    24696,  // mob_coilskar_witch
    24698   // mob_ethereum_smuggler
};

/*
0  - Selin Fireheart
1  - Vexallus
2  - Priestess Delrissa
3  - Kael'thas Sunstrider
4  - Kael'thas trash pack event
*/

struct TRINITY_DLL_DECL instance_magisters_terrace : public ScriptedInstance
{
    instance_magisters_terrace(Map* map) : ScriptedInstance(map) {Initialize();}

    uint8 KaelPhase;
    uint32 Encounters[ENCOUNTERS];
    uint32 DelrissaDeathCount;

    uint64 KaelGUID;
    uint64 SelinGUID;
    uint64 DelrissaGUID;
    uint64 VexallusDoorGUID;
    uint64 SelinDoorGUID;
    uint64 SelinEncounterDoorGUID;
    uint64 DelrissaDoorGUID;
    uint64 KaelStatue[2];

    void Initialize()
    {
        for(uint8 i = 0; i < ENCOUNTERS; i++)
            Encounters[i] = NOT_STARTED;

        DelrissaDeathCount = 0;

        KaelPhase = 0;
        KaelGUID = 0;
        SelinGUID = 0;
        DelrissaGUID = 0;
        VexallusDoorGUID = 0;
        SelinDoorGUID = 0;
        SelinEncounterDoorGUID = 0;
        DelrissaDoorGUID = 0;
        KaelStatue[0] = 0;
        KaelStatue[1] = 0;
    }

    bool IsEncounterInProgress() const
    {
        for(uint8 i = 0; i < ENCOUNTERS; i++)
            if(Encounters[i] == IN_PROGRESS)
                return true;
        return false;
    }

    uint32 GetData(uint32 identifier)
    {
        switch(identifier)
        {
            case DATA_SELIN_EVENT:          return Encounters[0];
            case DATA_VEXALLUS_EVENT:       return Encounters[1];
            case DATA_DELRISSA_EVENT:       return Encounters[2];
            case DATA_KAELTHAS_EVENT:       return Encounters[3];
            case DATA_DELRISSA_DEATH_COUNT: return DelrissaDeathCount;
            case DATA_KAEL_PHASE:           return KaelPhase;
        }
        return 0;
    }

    void SetData(uint32 identifier, uint32 data)
    {
        switch(identifier)
        {
            case DATA_SELIN_EVENT:
                if(Encounters[0] != DONE)
                    Encounters[0] = data;
                if(data == DONE)
                    HandleGameObject(SelinDoorGUID, true);
                HandleGameObject(SelinEncounterDoorGUID, data != IN_PROGRESS);
                break;
            case DATA_VEXALLUS_EVENT:
                if(Encounters[1] != DONE)
                    Encounters[1] = data;
                if(data == DONE)
                    HandleGameObject(VexallusDoorGUID, true);
                break;
            case DATA_DELRISSA_EVENT:
                if(Encounters[2] != DONE)
                    Encounters[2] = data;
                if(data == DONE)
                    HandleGameObject(DelrissaDoorGUID, true);
                break;
            case DATA_KAELTHAS_EVENT:
                if(Encounters[3] != DONE)
                    Encounters[3] = data;
                break;
            case DATA_DELRISSA_DEATH_COUNT:
                if(data)
                    ++DelrissaDeathCount;
                else
                    DelrissaDeathCount = 0;
                break;
            case DATA_KAEL_PHASE:
                KaelPhase = data;
                break;
        }

        if(data == DONE)
            SaveToDB();
    }

    std::string GetSaveData()
    {
        OUT_SAVE_INST_DATA;

        std::ostringstream stream;
        stream << Encounters[0] << " ";
        stream << Encounters[1] << " ";
        stream << Encounters[2] << " ";
        stream << Encounters[3];

        OUT_SAVE_INST_DATA_COMPLETE;

        return stream.str();
    }

    void Load(const char* in)
    {
        if(!in)
        {
            OUT_LOAD_INST_DATA_FAIL;
            return;
        }
        OUT_LOAD_INST_DATA(in);
        std::istringstream stream(in);
        stream >> Encounters[0] >> Encounters[1] >> Encounters[2] >> Encounters[3];
        for(uint8 i = 0; i < ENCOUNTERS; ++i)
            if(Encounters[i] == IN_PROGRESS)
                Encounters[i] = NOT_STARTED;
        OUT_LOAD_INST_DATA_COMPLETE;
    }

    void OnCreatureCreate(Creature *creature, uint32 entry)
    {
        switch(entry)
        {
            case 24723: SelinGUID = creature->GetGUID(); break;
            case 24560: DelrissaGUID = creature->GetGUID(); break;
            case 24664: KaelGUID = creature->GetGUID(); break;
        }
    }

    void OnObjectCreate(GameObject* go)
    {
        switch(go->GetEntry())
        {
            // Vexallus Doors
            case 187896:
                VexallusDoorGUID = go->GetGUID();
                go->SetGoState(GOState(GetData(DATA_VEXALLUS_EVENT) != DONE));
                break;
            // SunwellRaid Gate 02
            case 187979:
                SelinDoorGUID = go->GetGUID();
                go->SetGoState(GOState(GetData(DATA_SELIN_EVENT) != DONE));
                break;
            // Assembly Chamber Door
            case 188065:
                SelinEncounterDoorGUID = go->GetGUID();
                break;
            case 187770:
                DelrissaDoorGUID = go->GetGUID();
                go->SetGoState(GOState(GetData(DATA_DELRISSA_EVENT) != DONE));
                break;
            // Left Statue
            case 188165:
                KaelStatue[0] = go->GetGUID();
                go->SetGoState(GOState(GetData(DATA_KAELTHAS_EVENT) != DONE));
                break;
            // Right Statue
            case 188166:
                KaelStatue[1] = go->GetGUID();
                go->SetGoState(GOState(GetData(DATA_KAELTHAS_EVENT) != DONE));
                break;
        }
    }

    uint64 GetData64(uint32 identifier)
    {
        switch(identifier)
        {
            case DATA_SELIN:                return SelinGUID;
            case DATA_KAEL:                 return KaelGUID;
            case DATA_DELRISSA:             return DelrissaGUID;
            case DATA_VEXALLUS_DOOR:        return VexallusDoorGUID;
            case DATA_SELIN_DOOR:           return SelinDoorGUID;
            case DATA_SELIN_ENCOUNTER_DOOR: return SelinEncounterDoorGUID;
            case DATA_DELRISSA_DOOR:        return DelrissaDoorGUID;
            case DATA_KAEL_STATUE_LEFT:     return KaelStatue[0];
            case DATA_KAEL_STATUE_RIGHT:    return KaelStatue[1];
        }
        return 0;
    }
};

InstanceData* GetInstanceData_instance_magisters_terrace(Map* map)
{
    return new instance_magisters_terrace(map);
}

void AddSC_instance_magisters_terrace()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "instance_magisters_terrace";
    newscript->GetInstanceData = &GetInstanceData_instance_magisters_terrace;
    newscript->RegisterSelf();
}

