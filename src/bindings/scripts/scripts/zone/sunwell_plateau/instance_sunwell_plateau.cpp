/* Copyright (C) 2006 - 2008 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 * This program is free software licensed under GPL version 2
 * Please see the included DOCS/LICENSE.TXT for more information */

/* ScriptData
SDName: Instance_Sunwell_Plateau
SD%Complete: 20
SDComment: VERIFY SCRIPT, rename Gates
SDCategory: Sunwell_Plateau
EndScriptData */

#include "precompiled.h"
#include "def_sunwell_plateau.h"

#define ENCOUNTERS 7

enum GoState
{
    CLOSE    = 1,
    OPEN    = 0
};

/* Sunwell Plateau:
0 - Kalecgos and Sathrovarr
1 - Brutallus & Madrigosa intro
2 - Brutallus
3 - Felmyst
4 - Eredar Twins (Alythess and Sacrolash)
5 - M'uru
6 - Kil'Jaeden
*/

struct TRINITY_DLL_DECL instance_sunwell_plateau : public ScriptedInstance
{
    instance_sunwell_plateau(Map *map) : ScriptedInstance(map) {Initialize();};

    uint32 Encounters[ENCOUNTERS];

    /** Creatures **/
    uint64 Kalecgos_Dragon;
    uint64 Kalecgos_Human;
    uint64 Sathrovarr;
    uint64 Brutallus;
    uint64 Madrigosa;
    uint64 BrutallusTrigger;
    uint64 Felmyst;
    uint64 Alythess;
    uint64 Sacrolash;
    uint64 Muru;
    uint64 KilJaeden;
    uint64 KilJaedenController;
    uint64 Anveena;
    uint64 KalecgosKJ;

    /** GameObjects **/
    uint64 ForceField;                                      // Kalecgos Encounter
    uint64 Collision_1;                                     // Kalecgos Encounter
    uint64 Collision_2;                                     // Kalecgos Encounter
    uint64 FireBarrier;                                     // Brutallus Encounter
    uint64 IceBarrier;                                      // Brutallus Encounter
    uint64 Gate[5];                                         // Rename this to be more specific after door placement is verified.

    /*** Misc ***/
    uint32 KalecgosPhase;

    uint32 EredarTwinsAliveInfo[2];

    void Initialize()
    {
        /*** Creatures ***/
        Kalecgos_Dragon         = 0;
        Kalecgos_Human          = 0;
        Sathrovarr              = 0;
        Brutallus               = 0;
        Madrigosa               = 0;
        BrutallusTrigger        = 0;
        Felmyst                 = 0;
        Alythess                = 0;
        Sacrolash               = 0;
        Muru                    = 0;
        KilJaeden               = 0;
        KilJaedenController     = 0;
        Anveena                 = 0;
        KalecgosKJ              = 0;

        /*** GameObjects ***/
        ForceField  = 0;
        Collision_1 = 0;
        Collision_2 = 0;
        FireBarrier = 0;
        IceBarrier = 0;
        Gate[0]     = 0;                                    // TODO: Rename Gate[n] with gate_<boss name> for better specificity
        Gate[1]     = 0;
        Gate[2]     = 0;
        Gate[3]     = 0;
        Gate[4]     = 0;

        EredarTwinsAliveInfo[0] = 0;
        EredarTwinsAliveInfo[1] = 0;

        /*** Encounters ***/
        for(uint8 i = 0; i < ENCOUNTERS; ++i)
            Encounters[i] = NOT_STARTED;
    }

    bool IsEncounterInProgress() const
    {
        for(uint8 i = 0; i < ENCOUNTERS; ++i)
            if(Encounters[i] == IN_PROGRESS)
                return true;

        return false;
    }

    Player* GetPlayerInMap()
    {
        Map::PlayerList const& players = instance->GetPlayers();

        if (!players.isEmpty())
        {
            for(Map::PlayerList::const_iterator itr = players.begin(); itr != players.end(); ++itr)
            {
                Player* plr = itr->getSource();
                if (plr && !plr->HasAura(45839,0))
                        return plr;
            }
        }

        debug_log("TSCR: Instance Sunwell Plateau: GetPlayerInMap, but PlayerList is empty!");
        return NULL;
    }

    void HandleGameObject(uint64 guid, uint32 state)
    {
        Player *player = GetPlayerInMap();

        if (!player || !guid)
        {
            debug_log("TSCR: Sunwell Plateau: HandleGameObject fail");
            return;
        }

        if (GameObject *go = GameObject::GetGameObject(*player,guid))
            go->SetGoState(GOState(state));
    }

    uint32 GetEncounterForEntry(uint32 entry)
    {
        switch(entry)
        {
            case 25315:
                return DATA_KILJAEDEN_EVENT;
            case 25741:
            case 25840:
                return DATA_MURU_EVENT;
            case 25166:
            case 25165:
                return DATA_EREDAR_TWINS_EVENT;
            case 25038:
                return DATA_FELMYST_EVENT;
            case 24882:
                return DATA_BRUTALLUS_EVENT;
            case 24850:
            case 24892:
                return DATA_KALECGOS_EVENT;
            default:
                return 0;
        }
    }

    void OnCreatureCreate(Creature* creature, uint32 entry)
    {
        if(creature->GetTypeId() == TYPEID_UNIT)    // just in case of something weird happening
            creature->CastSpell(creature, SPELL_SUNWELL_RADIANCE, true);
        switch(entry)
        {
            case 24850: Kalecgos_Dragon     = creature->GetGUID(); break;
            case 24891: Kalecgos_Human      = creature->GetGUID(); break;
            case 24892: Sathrovarr          = creature->GetGUID(); break;
            case 24882: Brutallus           = creature->GetGUID(); break;
            case 25166: Alythess            = creature->GetGUID(); break;
            case 25165: Sacrolash           = creature->GetGUID(); break;
            case 25741: Muru                = creature->GetGUID(); break;
            case 25315: KilJaeden           = creature->GetGUID(); break;
            case 25608: KilJaedenController = creature->GetGUID(); break;
            case 26046: Anveena             = creature->GetGUID(); break;
            case 25319: KalecgosKJ          = creature->GetGUID(); break;
            // if Felmyst GUID exists, do not summom on Madrigosa create
            case 25038: Felmyst             = creature->GetGUID(); break;
            case 24895:
                //TODO: Proper reseting when Felmyst not summoned, Brutallus not killed, etc.
                Madrigosa = creature->GetGUID();
                if(GetData(DATA_BRUTALLUS_INTRO_EVENT) == DONE)
                {
                    creature->setFaction(35);
                    creature->SetVisibility(VISIBILITY_OFF);
                }
                break;
                if(GetData(DATA_BRUTALLUS_EVENT) == DONE || GetData(DATA_FELMYST_EVENT) != DONE)
                {
                    // summon Felmyst
                    if(!Felmyst)
                    {
                        creature->CastSpell(creature, 45069, true);/*
                        float x, y, z;
                        creature->GetPosition(x, y, z);
                        creature->UpdateAllowedPositionZ(x, y, z);
                        if(Creature* trigger = creature->SummonTrigger(x, y, z, 0, 10000))
                            trigger->CastSpell(trigger, 45069, true);*/
                    }
                }
            case 19871: BrutallusTrigger    = creature->GetGUID(); break;
            /*case 25038:
                // rewrite this, Felmyst summoned by spell
                Felmyst = creature->GetGUID();
                if(GetData(DATA_BRUTALLUS_EVENT) != DONE)
                {
                    creature->setFaction(35);
                    creature->SetVisibility(VISIBILITY_OFF);
                }
                break;*/
        }

        const CreatureData *tmp = creature->GetLinkedRespawnCreatureData();
        if (!tmp)
            return;

        if (GetEncounterForEntry(tmp->id) && creature->isAlive() && GetData(GetEncounterForEntry(tmp->id)) == DONE)
            creature->Kill(creature, false);
    }

    void OnObjectCreate(GameObject* gobj)
    {
        switch(gobj->GetEntry())
        {
            case 188421: ForceField     = gobj->GetGUID(); break;
            case 188523: Collision_1    = gobj->GetGUID(); break;
            case 188524: Collision_2    = gobj->GetGUID(); break;
            case 188075: FireBarrier    = gobj->GetGUID(); break;
            case 188119: IceBarrier     = gobj->GetGUID(); break;
            case 187979: Gate[0]        = gobj->GetGUID(); break;
            case 187770: Gate[1]        = gobj->GetGUID(); break;
            case 187896: Gate[2]        = gobj->GetGUID(); break;
            case 187990: Gate[3]        = gobj->GetGUID(); break;
            case 188118: Gate[4]        = gobj->GetGUID(); break;
        }
    }

    uint32 GetData(uint32 id)
    {
        switch(id)
        {
            case DATA_KALECGOS_EVENT:           return Encounters[0]; break;
            case DATA_BRUTALLUS_INTRO_EVENT:    return Encounters[1]; break;
            case DATA_BRUTALLUS_EVENT:          return Encounters[2]; break;
            case DATA_FELMYST_EVENT:            return Encounters[3]; break;
            case DATA_EREDAR_TWINS_EVENT:       return Encounters[4]; break;
            case DATA_MURU_EVENT:               return Encounters[5]; break;
            case DATA_KILJAEDEN_EVENT:          return Encounters[6]; break;
            case DATA_KALECGOS_PHASE:           return KalecgosPhase; break;
            case DATA_ALYTHESS:                 return EredarTwinsAliveInfo[0];
            case DATA_SACROLASH:                return EredarTwinsAliveInfo[1];
        }

        return 0;
    }

    uint64 GetData64(uint32 id)
    {
        switch(id)
        {
            case DATA_KALECGOS_DRAGON:      return Kalecgos_Dragon;     break;
            case DATA_KALECGOS_HUMAN:       return Kalecgos_Human;      break;
            case DATA_SATHROVARR:           return Sathrovarr;          break;
            case DATA_BRUTALLUS:            return Brutallus;           break;
            case DATA_MADRIGOSA:            return Madrigosa;           break;
            case DATA_BRUTALLUS_TRIGGER:    return BrutallusTrigger;    break;
            case DATA_FELMYST:              return Felmyst;             break;
            case DATA_ALYTHESS:             return Alythess;            break;
            case DATA_SACROLASH:            return Sacrolash;           break;
            case DATA_MURU:                 return Muru;                break;
            case DATA_KILJAEDEN:            return KilJaeden;           break;
            case DATA_KILJAEDEN_CONTROLLER: return KilJaedenController; break;
            case DATA_ANVEENA:              return Anveena;             break;
            case DATA_KALECGOS_KJ:          return KalecgosKJ;          break;
            case DATA_PLAYER_GUID:
                Player* Target = GetPlayerInMap();
                return Target->GetGUID();
                break;
        }

        return 0;
    }

    void SetData(uint32 id, uint32 data)
    {
        switch(id)
        {
            case DATA_KALECGOS_EVENT:
                if(data == IN_PROGRESS)
                {
                    HandleGameObject(ForceField, CLOSE);
                    HandleGameObject(Collision_1, CLOSE);
                    HandleGameObject(Collision_2, CLOSE);
                }
                else
                {
                    HandleGameObject(ForceField, OPEN);
                    HandleGameObject(Collision_1, OPEN);
                    HandleGameObject(Collision_2, OPEN);
                }
                if(Encounters[0] != DONE)
                    Encounters[0] = data;
                break;
            case DATA_BRUTALLUS_INTRO_EVENT:
                if(Encounters[1] != DONE)
                    Encounters[1] = data;
                switch(data)
                {
                    case IN_PROGRESS:
                        HandleGameObject(IceBarrier, CLOSE);
                        break;
                    case DONE:
                        HandleGameObject(IceBarrier, OPEN);
                        break;
                }
                break;
            case DATA_BRUTALLUS_EVENT:
                if(Encounters[2] != DONE)
                    Encounters[2] = data;
                break;
            case DATA_FELMYST_EVENT:
                if(data == DONE)
                    HandleGameObject(FireBarrier, OPEN);
                if(Encounters[3] != DONE)
                    Encounters[3] = data;
                break;
            case DATA_EREDAR_TWINS_EVENT:
                if(Encounters[4] != DONE)
                    Encounters[4] = data;
                break;
            case DATA_MURU_EVENT:
                if(Encounters[5] != DONE)
                {
                    switch(data){
                        case DONE:
                            HandleGameObject(Gate[4], OPEN);
                            HandleGameObject(Gate[3], OPEN);
                            break;
                        case IN_PROGRESS:
                            HandleGameObject(Gate[4], CLOSE);
                            HandleGameObject(Gate[3], CLOSE);
                            break;
                        case NOT_STARTED:
                            HandleGameObject(Gate[4], CLOSE);
                            HandleGameObject(Gate[3], OPEN);
                            break;
                    }
                    Encounters[5] = data;
                }
                break;
            case DATA_KILJAEDEN_EVENT:
                if(Encounters[6] != DONE)
                    Encounters[6] = data;
                break;
            case DATA_KALECGOS_PHASE:
                KalecgosPhase = data; 
                break;
            case DATA_ALYTHESS:
                EredarTwinsAliveInfo[0] = data;

                if (data == DONE && IsEncounterInProgress())
                {
                    if (Creature *pSacrolash = GetCreature(GetData64(DATA_SACROLASH)))
                        pSacrolash->AI()->DoAction(SISTER_DEATH);
                }
                return;
            case DATA_SACROLASH:
                EredarTwinsAliveInfo[1] = data;
                if (data == DONE && IsEncounterInProgress())
                {
                    if (Creature *pAlythess = GetCreature(GetData64(DATA_ALYTHESS)))
                        pAlythess->AI()->DoAction(SISTER_DEATH);
                }
                return;
        }

        if(data == DONE)
            SaveToDB();
    }

    void SetData64(uint32 id, uint64 guid)
    {
    }

    void Update(uint32 diff)
    {
    }

    std::string GetSaveData()
    {
        OUT_SAVE_INST_DATA;

        std::ostringstream stream;
        stream << Encounters[0] << " ";
        stream << Encounters[1] << " ";
        stream << Encounters[2] << " ";
        stream << Encounters[3] << " ";
        stream << Encounters[4] << " ";
        stream << Encounters[5] << " ";
        stream << Encounters[6];

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
        stream >> Encounters[0] >> Encounters[1] >> Encounters[2] >> Encounters[3]
            >> Encounters[4] >> Encounters[5] >> Encounters[6];
        for(uint8 i = 0; i < ENCOUNTERS; ++i)
            if(Encounters[i] == IN_PROGRESS)                // Do not load an encounter as "In Progress" - reset it instead.
                Encounters[i] = NOT_STARTED;
        OUT_LOAD_INST_DATA_COMPLETE;
    }
};

InstanceData* GetInstanceData_instance_sunwell_plateau(Map* map)
{
    return new instance_sunwell_plateau(map);
}

void AddSC_instance_sunwell_plateau()
{
    Script *newscript;
    newscript = new Script;
    newscript->Name = "instance_sunwell_plateau";
    newscript->GetInstanceData = &GetInstanceData_instance_sunwell_plateau;
    newscript->RegisterSelf();
}

