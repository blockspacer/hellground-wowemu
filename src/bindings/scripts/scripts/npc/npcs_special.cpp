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
SDName: Npcs_Special
SD%Complete: 100
SDComment: To be used for special NPCs that are located globally. Support for quest 3861 (Cluck!), 6622 and 6624 (Triage)
SDCategory: NPCs
EndScriptData
*/

/* ContentData
npc_chicken_cluck       100%    support for quest 3861 (Cluck!)
npc_dancing_flames      100%    midsummer event NPC
npc_guardian            100%    guardianAI used to prevent players from accessing off-limits areas. Not in use by SD2
npc_injured_patient     100%    patients for triage-quests (6622 and 6624)
npc_doctor              100%    Gustaf Vanhowzen and Gregory Victor, quest 6622 and 6624 (Triage)
npc_mount_vendor        100%    Regular mount vendors all over the world. Display gossip if player doesn't meet the requirements to buy
npc_rogue_trainer       80%     Scripted trainers, so they are able to offer item 17126 for class quest 6681
npc_sayge               100%    Darkmoon event fortune teller, buff player based on answers given
npc_snake_trap_serpents 80%     AI for snakes that summoned by Snake Trap
npc_flight_master       100%    AI for flight masters.
npc_lazy_peon                   AI for peons for quest 5441 (Lazy Peons)
npc_mojo                %       AI for companion Mojo (summoned by item: 33993)
npc_master_omarion      100%    Master Craftsman Omarion, patterns menu
npc_lorekeeper_lydros   100%    Dialogue (story) + add A Dull and Flat Elven Blade
EndContentData */

#include "precompiled.h"
#include "Totem.h"

/*########
# npc_chicken_cluck
#########*/

#define QUEST_CLUCK         3861
#define EMOTE_A_HELLO       "looks up at you quizzically. Maybe you should inspect it?"
#define EMOTE_H_HELLO       "looks at you unexpectadly."
#define CLUCK_TEXT2         "starts pecking at the feed."
#define FACTION_FRIENDLY    84
#define FACTION_CHICKEN     31

struct TRINITY_DLL_DECL npc_chicken_cluckAI : public ScriptedAI
{
    npc_chicken_cluckAI(Creature *c) : ScriptedAI(c) {}

    uint32 ResetFlagTimer;

    void Reset()
    {
        ResetFlagTimer = 120000;

        m_creature->setFaction(FACTION_CHICKEN);
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
    }

    void EnterCombat(Unit *who) {}

    void UpdateAI(const uint32 diff)
    {
        // Reset flags after a certain time has passed so that the next player has to start the 'event' again
        if(m_creature->HasFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER))
        {
            if(ResetFlagTimer < diff)
            {
                EnterEvadeMode();
                return;
            }
            else
                ResetFlagTimer -= diff;
        }

        if(UpdateVictim())
            DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_chicken_cluck(Creature *_Creature)
{
    return new npc_chicken_cluckAI(_Creature);
}

bool ReceiveEmote_npc_chicken_cluck( Player *player, Creature *_Creature, uint32 emote )
{
    if( emote == TEXTEMOTE_CHICKEN )
    {
        if( player->GetTeam() == ALLIANCE )
        {
            if( rand()%30 == 1 )
            {
                if( player->GetQuestStatus(QUEST_CLUCK) == QUEST_STATUS_NONE )
                {
                    _Creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
                    _Creature->setFaction(FACTION_FRIENDLY);
                    _Creature->MonsterTextEmote(EMOTE_A_HELLO, 0);
                }
            }
        } else
        _Creature->MonsterTextEmote(EMOTE_H_HELLO,0);
    }
    if( emote == TEXTEMOTE_CHEER && player->GetTeam() == ALLIANCE )
        if( player->GetQuestStatus(QUEST_CLUCK) == QUEST_STATUS_COMPLETE )
    {
        _Creature->SetFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        _Creature->setFaction(FACTION_FRIENDLY);
        _Creature->MonsterTextEmote(CLUCK_TEXT2, 0);
    }

    return true;
}

bool QuestAccept_npc_chicken_cluck(Player *player, Creature *_Creature, const Quest *_Quest )
{
    if(_Quest->GetQuestId() == QUEST_CLUCK)
        ((npc_chicken_cluckAI*)_Creature->AI())->Reset();

    return true;
}

bool QuestComplete_npc_chicken_cluck(Player *player, Creature *_Creature, const Quest *_Quest)
{
    if(_Quest->GetQuestId() == QUEST_CLUCK)
        ((npc_chicken_cluckAI*)_Creature->AI())->Reset();

    return true;
}

/*######
## npc_dancing_flames
######*/

#define SPELL_BRAZIER       45423
#define SPELL_SEDUCTION     47057
#define SPELL_FIERY_AURA    45427

struct TRINITY_DLL_DECL npc_dancing_flamesAI : public ScriptedAI
{
    npc_dancing_flamesAI(Creature *c) : ScriptedAI(c) {}

    bool active;
    uint32 can_iteract;

    void Reset()
    {
        active = true;
        can_iteract = 3500;
        DoCast(m_creature,SPELL_BRAZIER,true);
        DoCast(m_creature,SPELL_FIERY_AURA,false);
        float x, y, z;
        m_creature->GetPosition(x,y,z);
        m_creature->Relocate(x,y,z + 0.94f);
        m_creature->AddUnitMovementFlag(MOVEMENTFLAG_ONTRANSPORT | MOVEMENTFLAG_LEVITATING);
        m_creature->HandleEmoteCommand(EMOTE_ONESHOT_DANCE);
        WorldPacket data;                       //send update position to client
        m_creature->BuildHeartBeatMsg(&data);
        m_creature->SendMessageToSet(&data,true);
    }

    void UpdateAI(const uint32 diff)
    {
        if (!active)
        {
            if(can_iteract <= diff){
                active = true;
                can_iteract = 3500;
                m_creature->HandleEmoteCommand(EMOTE_ONESHOT_DANCE);
            }
            else
                can_iteract -= diff;
        }
    }
};

CreatureAI* GetAI_npc_dancing_flames(Creature *_Creature)
{
    return new npc_dancing_flamesAI(_Creature);
}

bool ReceiveEmote_npc_dancing_flames( Player *player, Creature *flame, uint32 emote )
{
    if ( ((npc_dancing_flamesAI*)flame->AI())->active &&
        flame->IsWithinLOS(player->GetPositionX(),player->GetPositionY(),player->GetPositionZ()) && flame->IsWithinDistInMap(player,30.0f))
    {
        flame->SetInFront(player);
        ((npc_dancing_flamesAI*)flame->AI())->active = false;

        WorldPacket data;
        flame->BuildHeartBeatMsg(&data);
        flame->SendMessageToSet(&data,true);
        switch(emote)
        {
            case TEXTEMOTE_KISS:    flame->HandleEmoteCommand(EMOTE_ONESHOT_SHY); break;
            case TEXTEMOTE_WAVE:    flame->HandleEmoteCommand(EMOTE_ONESHOT_WAVE); break;
            case TEXTEMOTE_BOW:     flame->HandleEmoteCommand(EMOTE_ONESHOT_BOW); break;
            case TEXTEMOTE_JOKE:    flame->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH); break;
            case TEXTEMOTE_DANCE:
            {
                if (!player->HasAura(SPELL_SEDUCTION,0))
                    flame->CastSpell(player,SPELL_SEDUCTION,true);
            }
            break;
        }
    }
    return true;
}

/*######
## Triage quest
######*/

#define SAY_DOC1 "I'm saved! Thank you, doctor!"
#define SAY_DOC2 "HOORAY! I AM SAVED!"
#define SAY_DOC3 "Sweet, sweet embrace... take me..."

struct Location
{
    float x, y, z, o;
};

#define DOCTOR_ALLIANCE     12939

static Location AllianceCoords[]=
{
    {                                                       // Top-far-right bunk as seen from entrance
        -3757.38, -4533.05, 14.16, 3.62
    },
    {                                                       // Top-far-left bunk
        -3754.36, -4539.13, 14.16, 5.13
    },
    {                                                       // Far-right bunk
        -3749.54, -4540.25, 14.28, 3.34
    },
    {                                                       // Right bunk near entrance
        -3742.10, -4536.85, 14.28, 3.64
    },
    {                                                       // Far-left bunk
        -3755.89, -4529.07, 14.05, 0.57
    },
    {                                                       // Mid-left bunk
        -3749.51, -4527.08, 14.07, 5.26
    },
    {                                                       // Left bunk near entrance
        -3746.37, -4525.35, 14.16, 5.22
    },
};

#define ALLIANCE_COORDS     7

//alliance run to where
#define A_RUNTOX -3742.96
#define A_RUNTOY -4531.52
#define A_RUNTOZ 11.91

#define DOCTOR_HORDE    12920

static Location HordeCoords[]=
{
    {                                                       // Left, Behind
        -1013.75, -3492.59, 62.62, 4.34
    },
    {                                                       // Right, Behind
        -1017.72, -3490.92, 62.62, 4.34
    },
    {                                                       // Left, Mid
        -1015.77, -3497.15, 62.82, 4.34
    },
    {                                                       // Right, Mid
        -1019.51, -3495.49, 62.82, 4.34
    },
    {                                                       // Left, front
        -1017.25, -3500.85, 62.98, 4.34
    },
    {                                                      // Right, Front
        -1020.95, -3499.21, 62.98, 4.34
    }
};

#define HORDE_COORDS        6

//horde run to where
#define H_RUNTOX -1016.44
#define H_RUNTOY -3508.48
#define H_RUNTOZ 62.96

const uint32 AllianceSoldierId[3] =
{
    12938,                                                  // 12938 Injured Alliance Soldier
    12936,                                                  // 12936 Badly injured Alliance Soldier
    12937                                                   // 12937 Critically injured Alliance Soldier
};

const uint32 HordeSoldierId[3] =
{
    12923,                                                  //12923 Injured Soldier
    12924,                                                  //12924 Badly injured Soldier
    12925                                                   //12925 Critically injured Soldier
};

/*######
## npc_doctor (handles both Gustaf Vanhowzen and Gregory Victor)
######*/

struct TRINITY_DLL_DECL npc_doctorAI : public ScriptedAI
{
    npc_doctorAI(Creature *c) : ScriptedAI(c) {}

    uint64 Playerguid;

    uint32 SummonPatient_Timer;
    uint32 SummonPatientCount;
    uint32 PatientDiedCount;
    uint32 PatientSavedCount;

    bool Event;

    std::list<uint64> Patients;
    std::vector<Location*> Coordinates;

    void Reset()
    {
        Event = false;
        SummonPatient_Timer = 10000;
        PatientSavedCount = 0;
        PatientDiedCount = 0;
        Playerguid = 0;
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
    }

    void BeginEvent(Player* player);
    void PatientDied(Location* Point);
    void PatientSaved(Creature* soldier, Player* player, Location* Point);
    void UpdateAI(const uint32 diff);
};

/*#####
## npc_injured_patient (handles all the patients, no matter Horde or Alliance)
#####*/

struct TRINITY_DLL_DECL npc_injured_patientAI : public ScriptedAI
{
    npc_injured_patientAI(Creature *c) : ScriptedAI(c) {}

    uint64 Doctorguid;

    Location* Coord;

    void Reset()
    {
        Doctorguid = 0;

        Coord = NULL;
                                                            //no select
        m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                                                            //no regen health
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
                                                            //to make them lay with face down
        m_creature->SetUInt32Value(UNIT_FIELD_BYTES_1, PLAYER_STATE_DEAD);

        uint32 mobId = m_creature->GetEntry();

        switch (mobId)
        {                                                   //lower max health
            case 12923:
            case 12938:                                     //Injured Soldier
                m_creature->SetHealth(uint32(m_creature->GetMaxHealth()*.75));
                break;
            case 12924:
            case 12936:                                     //Badly injured Soldier
                m_creature->SetHealth(uint32(m_creature->GetMaxHealth()*.50));
                break;
            case 12925:
            case 12937:                                     //Critically injured Soldier
                m_creature->SetHealth(uint32(m_creature->GetMaxHealth()*.25));
                break;
        }
    }

    void SpellHit(Unit *caster, const SpellEntry *spell)
    {
        if (caster->GetTypeId() == TYPEID_PLAYER && m_creature->isAlive() && spell->Id == 20804)
        {
            if( (((Player*)caster)->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE) || (((Player*)caster)->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE))
            {
                if(Doctorguid)
                {
                    Creature* Doctor = Unit::GetCreature((*m_creature), Doctorguid);
                    if(Doctor)
                        ((npc_doctorAI*)Doctor->AI())->PatientSaved(m_creature, ((Player*)caster), Coord);
                }
            }
                                                            //make not selectable
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
                                                            //regen health
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
                                                            //stand up
            m_creature->SetUInt32Value(UNIT_FIELD_BYTES_1, PLAYER_STATE_NONE);
            DoSay(SAY_DOC1,LANG_UNIVERSAL,NULL);

            uint32 mobId = m_creature->GetEntry();
            m_creature->RemoveUnitMovementFlag(MOVEMENTFLAG_WALK_MODE);
            switch (mobId)
            {
                case 12923:
                case 12924:
                case 12925:
                    m_creature->GetMotionMaster()->MovePoint(0, H_RUNTOX, H_RUNTOY, H_RUNTOZ);
                    break;
                case 12936:
                case 12937:
                case 12938:
                    m_creature->GetMotionMaster()->MovePoint(0, A_RUNTOX, A_RUNTOY, A_RUNTOZ);
                    break;
            }
        }
        return;
    }

    void UpdateAI(const uint32 diff)
    {
        if (m_creature->isAlive() && m_creature->GetHealth() > 6)
        {                                                   //lower HP on every world tick makes it a useful counter, not officlone though
            m_creature->SetHealth(uint32(m_creature->GetHealth()-5) );
        }

        if (m_creature->isAlive() && m_creature->GetHealth() <= 6)
        {
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IN_COMBAT);
            m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
            m_creature->setDeathState(JUST_DIED);
            m_creature->SetFlag(UNIT_DYNAMIC_FLAGS, 32);

            if(Doctorguid)
            {
                Creature* Doctor = (Unit::GetCreature((*m_creature), Doctorguid));
                if(Doctor)
                    ((npc_doctorAI*)Doctor->AI())->PatientDied(Coord);
            }
        }
    }
};

CreatureAI* GetAI_npc_injured_patient(Creature *_Creature)
{
    return new npc_injured_patientAI (_Creature);
}

/*
npc_doctor (continue)
*/

void npc_doctorAI::BeginEvent(Player* player)
{
    Playerguid = player->GetGUID();

    SummonPatient_Timer = 10000;
    SummonPatientCount = 0;
    PatientDiedCount = 0;
    PatientSavedCount = 0;

    switch(m_creature->GetEntry())
    {
        case DOCTOR_ALLIANCE:
            for(uint8 i = 0; i < ALLIANCE_COORDS; ++i)
                Coordinates.push_back(&AllianceCoords[i]);
            break;

        case DOCTOR_HORDE:
            for(uint8 i = 0; i < HORDE_COORDS; ++i)
                Coordinates.push_back(&HordeCoords[i]);
            break;
    }

    Event = true;

    m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
}

void npc_doctorAI::PatientDied(Location* Point)
{
    Player* player = Unit::GetPlayer(Playerguid);
    if(player && ((player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE) || (player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)))
    {
        PatientDiedCount++;
        if (PatientDiedCount > 5 && Event)
        {
            if(player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE)
                player->FailQuest(6624);
            else if(player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)
                player->FailQuest(6622);

            Event = false;
            m_creature->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NOT_SELECTABLE);
        }

        Coordinates.push_back(Point);
    }
    else
         Reset();
}

void npc_doctorAI::PatientSaved(Creature* soldier, Player* player, Location* Point)
{
    if(player && Playerguid == player->GetGUID())
    {
        if((player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE) || (player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE))
        {
            PatientSavedCount++;
            if(PatientSavedCount == 15)
            {
                if(!Patients.empty())
                {
                    std::list<uint64>::iterator itr;
                    for(itr = Patients.begin(); itr != Patients.end(); ++itr)
                    {
                        Creature* Patient = Unit::GetCreature((*m_creature), *itr);
                        if( Patient )
                            Patient->setDeathState(JUST_DIED);
                    }
                }

                if(player->GetQuestStatus(6624) == QUEST_STATUS_INCOMPLETE)
                    player->AreaExploredOrEventHappens(6624);
                else if(player->GetQuestStatus(6622) == QUEST_STATUS_INCOMPLETE)
                    player->AreaExploredOrEventHappens(6622);

                Reset();
            }

            Coordinates.push_back(Point);
        }
    }
    else
         Reset();
}

void npc_doctorAI::UpdateAI(const uint32 diff)
{
    if(Event && SummonPatientCount >= 20)
         Reset();

    if(Event)
        if(SummonPatient_Timer < diff)
    {
        Creature* Patient = NULL;
        Location* Point = NULL;

        if(Coordinates.empty())
            return;

        std::vector<Location*>::iterator itr = Coordinates.begin()+rand()%Coordinates.size();
        uint32 patientEntry = 0;

        switch(m_creature->GetEntry())
        {
            case DOCTOR_ALLIANCE: patientEntry = AllianceSoldierId[rand()%3]; break;
            case DOCTOR_HORDE:    patientEntry = HordeSoldierId[rand()%3]; break;
            default:
                error_log("TSCR: Invalid entry for Triage doctor. Please check your database");
                return;
        }

        Point = *itr;

        Patient = m_creature->SummonCreature(patientEntry, Point->x, Point->y, Point->z, Point->o, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 5000);

        if(Patient)
        {
            Patients.push_back(Patient->GetGUID());
            ((npc_injured_patientAI*)Patient->AI())->Doctorguid = m_creature->GetGUID();
            if(Point)
                ((npc_injured_patientAI*)Patient->AI())->Coord = Point;
            Coordinates.erase(itr);
        }
        SummonPatient_Timer = 10000;
        SummonPatientCount++;
    }
    else
        SummonPatient_Timer -= diff;
}

bool QuestAccept_npc_doctor(Player *player, Creature *creature, Quest const *quest )
{
    if((quest->GetQuestId() == 6624) || (quest->GetQuestId() == 6622))
        ((npc_doctorAI*)creature->AI())->BeginEvent(player);

    return true;
}

CreatureAI* GetAI_npc_doctor(Creature *_Creature)
{
    return new npc_doctorAI (_Creature);
}

/*######
## npc_guardian
######*/

#define SPELL_DEATHTOUCH                5
#define SAY_AGGRO                        "This area is closed!"

struct TRINITY_DLL_DECL npc_guardianAI : public ScriptedAI
{
    npc_guardianAI(Creature *c) : ScriptedAI(c) {}

    void Reset()
    {
        m_creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
    }

    void EnterCombat(Unit *who)
    {
        DoYell(SAY_AGGRO,LANG_UNIVERSAL,NULL);
    }

    void UpdateAI(const uint32 diff)
    {
        if (!UpdateVictim())
            return;

        if (m_creature->isAttackReady())
        {
            m_creature->CastSpell(m_creature->getVictim(),SPELL_DEATHTOUCH, true);
            m_creature->resetAttackTimer();
        }
    }
};

CreatureAI* GetAI_npc_guardian(Creature *_Creature)
{
    return new npc_guardianAI (_Creature);
}

/*######
## npc_mount_vendor
######*/

bool GossipHello_npc_mount_vendor(Player *player, Creature *_Creature)
{
    if (_Creature->isQuestGiver())
        player->PrepareQuestMenu( _Creature->GetGUID() );

    bool canBuy;
    canBuy = false;
    uint32 vendor = _Creature->GetEntry();
    uint8 race = player->getRace();

    switch (vendor)
    {
        case 384:                                           //Katie Hunter
        case 1460:                                          //Unger Statforth
        case 2357:                                          //Merideth Carlson
        case 4885:                                          //Gregor MacVince
            if (player->GetReputationRank(72) != REP_EXALTED && race != RACE_HUMAN)
                player->SEND_GOSSIP_MENU(5855, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 1261:                                          //Veron Amberstill
            if (player->GetReputationRank(47) != REP_EXALTED && race != RACE_DWARF)
                player->SEND_GOSSIP_MENU(5856, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 3362:                                          //Ogunaro Wolfrunner
            if (player->GetReputationRank(76) != REP_EXALTED && race != RACE_ORC)
                player->SEND_GOSSIP_MENU(5841, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 3685:                                          //Harb Clawhoof
            if (player->GetReputationRank(81) != REP_EXALTED && race != RACE_TAUREN)
                player->SEND_GOSSIP_MENU(5843, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 4730:                                          //Lelanai
            if (player->GetReputationRank(69) != REP_EXALTED && race != RACE_NIGHTELF)
                player->SEND_GOSSIP_MENU(5844, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 4731:                                          //Zachariah Post
            if (player->GetReputationRank(68) != REP_EXALTED && race != RACE_UNDEAD_PLAYER)
                player->SEND_GOSSIP_MENU(5840, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 7952:                                          //Zjolnir
            if (player->GetReputationRank(530) != REP_EXALTED && race != RACE_TROLL)
                player->SEND_GOSSIP_MENU(5842, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 7955:                                          //Milli Featherwhistle
            if (player->GetReputationRank(54) != REP_EXALTED && race != RACE_GNOME)
                player->SEND_GOSSIP_MENU(5857, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 16264:                                         //Winaestra
            if (player->GetReputationRank(911) != REP_EXALTED && race != RACE_BLOODELF)
                player->SEND_GOSSIP_MENU(10305, _Creature->GetGUID());
            else canBuy = true;
            break;
        case 17584:                                         //Torallius the Pack Handler
            if (player->GetReputationRank(930) != REP_EXALTED && race != RACE_DRAENEI)
                player->SEND_GOSSIP_MENU(10239, _Creature->GetGUID());
            else canBuy = true;
            break;
    }

    if (canBuy)
    {
        if (_Creature->isVendor())
            player->ADD_GOSSIP_ITEM( 1, GOSSIP_TEXT_BROWSE_GOODS, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRADE);
        player->SEND_GOSSIP_MENU(_Creature->GetNpcTextId(), _Creature->GetGUID());
    }
    return true;
}

bool GossipSelect_npc_mount_vendor(Player *player, Creature *_Creature, uint32 sender, uint32 action)
{
    if (action == GOSSIP_ACTION_TRADE)
        player->SEND_VENDORLIST( _Creature->GetGUID() );

    return true;
}

/*######
## npc_rogue_trainer
######*/

bool GossipHello_npc_rogue_trainer(Player *player, Creature *_Creature)
{
    if( _Creature->isQuestGiver() )
        player->PrepareQuestMenu( _Creature->GetGUID() );

    if( _Creature->isTrainer() )
        player->ADD_GOSSIP_ITEM(2, GOSSIP_TEXT_TRAIN, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_TRAIN);

    if( _Creature->isCanTrainingAndResetTalentsOf(player) )
        player->ADD_GOSSIP_ITEM(2, "I wish to unlearn my talents", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_UNLEARNTALENTS);

    if( player->getClass() == CLASS_ROGUE && player->getLevel() >= 24 && !player->HasItemCount(17126,1) && !player->GetQuestRewardStatus(6681) )
    {
        player->ADD_GOSSIP_ITEM(0, "<Take the letter>", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+1);
        player->SEND_GOSSIP_MENU(5996, _Creature->GetGUID());
    } else
        player->SEND_GOSSIP_MENU(_Creature->GetNpcTextId(), _Creature->GetGUID());

    return true;
}

bool GossipSelect_npc_rogue_trainer(Player *player, Creature *_Creature, uint32 sender, uint32 action)
{
    switch( action )
    {
        case GOSSIP_ACTION_INFO_DEF+1:
            player->CLOSE_GOSSIP_MENU();
            player->CastSpell(player,21100,false);
            break;
        case GOSSIP_ACTION_TRAIN:
            player->SEND_TRAINERLIST( _Creature->GetGUID() );
            break;
        case GOSSIP_OPTION_UNLEARNTALENTS:
            player->CLOSE_GOSSIP_MENU();
            player->SendTalentWipeConfirm( _Creature->GetGUID() );
            break;
    }
    return true;
}

/*######
## npc_sayge
######*/

#define SPELL_DMG       23768                               //dmg
#define SPELL_RES       23769                               //res
#define SPELL_ARM       23767                               //arm
#define SPELL_SPI       23738                               //spi
#define SPELL_INT       23766                               //int
#define SPELL_STM       23737                               //stm
#define SPELL_STR       23735                               //str
#define SPELL_AGI       23736                               //agi
#define SPELL_FORTUNE   23765                               //faire fortune

bool GossipHello_npc_sayge(Player *player, Creature *_Creature)
{
    if(_Creature->isQuestGiver())
        player->PrepareQuestMenu( _Creature->GetGUID() );

    if( player->HasSpellCooldown(SPELL_INT) ||
        player->HasSpellCooldown(SPELL_ARM) ||
        player->HasSpellCooldown(SPELL_DMG) ||
        player->HasSpellCooldown(SPELL_RES) ||
        player->HasSpellCooldown(SPELL_STR) ||
        player->HasSpellCooldown(SPELL_AGI) ||
        player->HasSpellCooldown(SPELL_STM) ||
        player->HasSpellCooldown(SPELL_SPI) )
        player->SEND_GOSSIP_MENU(7393, _Creature->GetGUID());
    else
    {
        player->ADD_GOSSIP_ITEM(0, "Yes", GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+1);
        player->SEND_GOSSIP_MENU(7339, _Creature->GetGUID());
    }

    return true;
}

void SendAction_npc_sayge(Player *player, Creature *_Creature, uint32 action)
{
    switch(action)
    {
        case GOSSIP_ACTION_INFO_DEF+1:
            player->ADD_GOSSIP_ITEM(0, "Slay the Man",                      GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
            player->ADD_GOSSIP_ITEM(0, "Turn him over to liege",            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+3);
            player->ADD_GOSSIP_ITEM(0, "Confiscate the corn",               GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+4);
            player->ADD_GOSSIP_ITEM(0, "Let him go and have the corn",      GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+5);
            player->SEND_GOSSIP_MENU(7340, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+2:
            player->ADD_GOSSIP_ITEM(0, "Execute your friend painfully",     GOSSIP_SENDER_MAIN+1, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Execute your friend painlessly",    GOSSIP_SENDER_MAIN+2, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Let your friend go",                GOSSIP_SENDER_MAIN+3, GOSSIP_ACTION_INFO_DEF);
            player->SEND_GOSSIP_MENU(7341, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+3:
            player->ADD_GOSSIP_ITEM(0, "Confront the diplomat",             GOSSIP_SENDER_MAIN+4, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Show not so quiet defiance",        GOSSIP_SENDER_MAIN+5, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Remain quiet",                      GOSSIP_SENDER_MAIN+2, GOSSIP_ACTION_INFO_DEF);
            player->SEND_GOSSIP_MENU(7361, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+4:
            player->ADD_GOSSIP_ITEM(0, "Speak against your brother openly", GOSSIP_SENDER_MAIN+6, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Help your brother in",              GOSSIP_SENDER_MAIN+7, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Keep your brother out without letting him know", GOSSIP_SENDER_MAIN+8, GOSSIP_ACTION_INFO_DEF);
            player->SEND_GOSSIP_MENU(7362, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+5:
            player->ADD_GOSSIP_ITEM(0, "Take credit, keep gold",            GOSSIP_SENDER_MAIN+5, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Take credit, share the gold",       GOSSIP_SENDER_MAIN+4, GOSSIP_ACTION_INFO_DEF);
            player->ADD_GOSSIP_ITEM(0, "Let the knight take credit",        GOSSIP_SENDER_MAIN+3, GOSSIP_ACTION_INFO_DEF);
            player->SEND_GOSSIP_MENU(7363, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF:
            player->ADD_GOSSIP_ITEM(0, "Thanks",                            GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+6);
            player->SEND_GOSSIP_MENU(7364, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+6:
            _Creature->CastSpell(player, SPELL_FORTUNE, false);
            player->SEND_GOSSIP_MENU(7365, _Creature->GetGUID());
            break;
    }
}

bool GossipSelect_npc_sayge(Player *player, Creature *_Creature, uint32 sender, uint32 action )
{
    switch(sender)
    {
        case GOSSIP_SENDER_MAIN:
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+1:
            _Creature->CastSpell(player, SPELL_DMG, false);
            player->AddSpellCooldown(SPELL_DMG,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+2:
            _Creature->CastSpell(player, SPELL_RES, false);
            player->AddSpellCooldown(SPELL_RES,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+3:
            _Creature->CastSpell(player, SPELL_ARM, false);
            player->AddSpellCooldown(SPELL_ARM,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+4:
            _Creature->CastSpell(player, SPELL_SPI, false);
            player->AddSpellCooldown(SPELL_SPI,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+5:
            _Creature->CastSpell(player, SPELL_INT, false);
            player->AddSpellCooldown(SPELL_INT,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+6:
            _Creature->CastSpell(player, SPELL_STM, false);
            player->AddSpellCooldown(SPELL_STM,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+7:
            _Creature->CastSpell(player, SPELL_STR, false);
            player->AddSpellCooldown(SPELL_STR,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
        case GOSSIP_SENDER_MAIN+8:
            _Creature->CastSpell(player, SPELL_AGI, false);
            player->AddSpellCooldown(SPELL_AGI,0,time(NULL) + 7200);
            SendAction_npc_sayge(player, _Creature, action);
            break;
    }
    return true;
}

struct TRINITY_DLL_DECL npc_steam_tonkAI : public ScriptedAI
{
    npc_steam_tonkAI(Creature *c) : ScriptedAI(c) {}

    void Reset() {}
    void EnterCombat(Unit *who) {}

    void OnPossess(bool apply)
    {
        if (apply)
        {
            // Initialize the action bar without the melee attack command
            m_creature->InitCharmInfo();
            m_creature->GetCharmInfo()->InitEmptyActionBar(false);

            m_creature->SetReactState(REACT_PASSIVE);
        }
        else
            m_creature->SetReactState(REACT_AGGRESSIVE);
    }

};

CreatureAI* GetAI_npc_steam_tonk(Creature *_Creature)
{
    return new npc_steam_tonkAI(_Creature);
}

#define SPELL_TONK_MINE_DETONATE 25099

struct TRINITY_DLL_DECL npc_tonk_mineAI : public ScriptedAI
{
    npc_tonk_mineAI(Creature *c) : ScriptedAI(c)
    {
        m_creature->SetReactState(REACT_PASSIVE);
    }

    uint32 ExplosionTimer;

    void Reset()
    {
        ExplosionTimer = 3000;
    }

    void EnterCombat(Unit *who) {}
    void AttackStart(Unit *who) {}
    void MoveInLineOfSight(Unit *who) {}

    void UpdateAI(const uint32 diff)
    {
        if (ExplosionTimer < diff)
        {
            m_creature->CastSpell(m_creature, SPELL_TONK_MINE_DETONATE, true);
            m_creature->setDeathState(DEAD); // unsummon it
        } else
            ExplosionTimer -= diff;
    }
};

CreatureAI* GetAI_npc_tonk_mine(Creature *_Creature)
{
    return new npc_tonk_mineAI(_Creature);
}

/*####
## npc_winter_reveler
####*/

bool ReceiveEmote_npc_winter_reveler( Player *player, Creature *_Creature, uint32 emote )
{
    if (player->HasSpellCooldown(26218))
        return false;

    if (emote == TEXTEMOTE_KISS)
    {
        _Creature->CastSpell(_Creature, 26218, false);
        player->CastSpell(player, 26218, false);

        switch(urand(0,3))
        {
            case 0: player->CastSpell(player, 26207, false); break;
            case 1: player->CastSpell(player, 26206, false); break;
            case 2: player->CastSpell(player, 45036, false); break;
        }
        player->AddSpellCooldown(26218, 0, time(NULL) + 3600);
    }
    return true;
}

/*####
## npc_brewfest_reveler
####*/

bool ReceiveEmote_npc_brewfest_reveler( Player *player, Creature *_Creature, uint32 emote )
{
    if( emote == TEXTEMOTE_DANCE )
        _Creature->CastSpell(player, 41586, false);

    return true;
}

/*####
## npc_snake_trap_serpents
####*/

#define SPELL_MIND_NUMBING_POISON    8692    //Viper
#define SPELL_DEADLY_POISON          34655   //Venomous Snake
#define SPELL_CRIPPLING_POISON       3409    //Viper

#define VENOMOUS_SNAKE_TIMER 1200
#define VIPER_TIMER 3000

#define C_VIPER 19921

#define RAND 5

struct TRINITY_DLL_DECL npc_snake_trap_serpentsAI : public ScriptedAI
{
    npc_snake_trap_serpentsAI(Creature *c) : ScriptedAI(c) {}

    uint32 SpellTimer;
    Unit *Owner;
    bool IsViper;

    void EnterCombat(Unit *who) {}

    void Reset()
    {
        Owner = m_creature->GetOwner();

        if (!m_creature->isPet() || !Owner)
            return;

        CreatureInfo const *Info = m_creature->GetCreatureInfo();

        if(Info->Entry == C_VIPER)
            IsViper = true;

        //Add delta to make them not all hit the same time
        uint32 delta = (rand() % 7) *100;
        m_creature->SetStatFloatValue(UNIT_FIELD_BASEATTACKTIME, Info->baseattacktime + delta);
        m_creature->SetStatFloatValue(UNIT_FIELD_RANGED_ATTACK_POWER , Info->attackpower);
    }

    //Redefined for random target selection:
    void MoveInLineOfSight(Unit *who)
    {
        if (!m_creature->isPet() || !Owner)
            return;

        if( !m_creature->getVictim() && who->isTargetableForAttack() && ( m_creature->IsHostileTo( who )) && who->isInAccessiblePlaceFor(m_creature) && Owner->IsHostileTo(who))//don't attack not-pvp-flaged
        {
            if (m_creature->GetDistanceZ(who) > CREATURE_Z_ATTACK_RANGE)
                return;

            float attackRadius = m_creature->GetAttackDistance(who);
            if( m_creature->IsWithinDistInMap(who, attackRadius) && m_creature->IsWithinLOSInMap(who) )
            {
                if (!(rand() % RAND) )
                {
                    m_creature->setAttackTimer(BASE_ATTACK, (rand() % 10) * 100);
                    SpellTimer = (rand() % 10) * 100;
                    AttackStart(who);
                }
            }
        }
    }

    void UpdateAI(const uint32 diff)
    {
        if (!m_creature->isPet() || !Owner)
            return;

        //Follow if not in combat
        if (!m_creature->hasUnitState(UNIT_STAT_FOLLOW) && !m_creature->isInCombat())
        {
            m_creature->GetMotionMaster()->Clear();
            m_creature->GetMotionMaster()->MoveFollow(Owner,PET_FOLLOW_DIST,PET_FOLLOW_ANGLE);
        }

        //No victim -> get new from owner (need this because MoveInLineOfSight won't work while following -> corebug)
        if (!m_creature->getVictim())
        {
            if (m_creature->isInCombat())
                DoStopAttack();

            if(Owner->getAttackerForHelper())
                AttackStart(Owner->getAttackerForHelper());

            return;
        }

        if (SpellTimer < diff)
        {
            if (IsViper) //Viper
            {
                if (rand() % 3 == 0) //33% chance to cast
                {
                    uint32 spell;
                    if( rand() % 2 == 0)
                        spell = SPELL_MIND_NUMBING_POISON;
                    else
                        spell = SPELL_CRIPPLING_POISON;

                    DoCast(m_creature->getVictim(),spell);
                }

                SpellTimer = VIPER_TIMER;
            }
            else //Venomous Snake
            {
                if (rand() % 10 < 8) //80% chance to cast
                    DoCast(m_creature->getVictim(),SPELL_DEADLY_POISON);
                SpellTimer = VENOMOUS_SNAKE_TIMER + (rand() %5)*100;
            }
        }
        else
            SpellTimer-=diff;

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_snake_trap_serpents(Creature *_Creature)
{
    return new npc_snake_trap_serpentsAI(_Creature);
}

/*################
# Flight Master  #
################*/

uint32 ADVISOR[] =
{
    9297,      // ENRAGED_WYVERN
    9526,      // ENRAGED_GRYPHON
    9521,      // ENRAGED_FELBAT
    9527,      // ENRAGED_HIPPOGRYPH
    27946      // SILVERMOON_DRAGONHAWK
};
const char type[] = "WGBHD";

struct TRINITY_DLL_DECL npc_flight_masterAI : public ScriptedAI
{
    npc_flight_masterAI(Creature *c) : ScriptedAI(c) {}

    void Reset(){}
    void SummonAdvisor()
    {
        const char *subname = m_creature->GetSubName();
        for(uint8 i = 0; i<5; i++)
        {
            if(subname[0] == type[i])
            {
               DoSpawnCreature(ADVISOR[i], 0, 0, 0, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1);
               DoSpawnCreature(ADVISOR[i], 0, 0, 0, 0, TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT, 1);
               break;
            }
        }
    }
    void JustSummoned(Creature *add)
    {
       if(add)
       {
           add->setFaction(m_creature->getFaction());
           add->SetLevel(m_creature->getLevel());
           add->AI()->AttackStart(m_creature->getVictim());
       }
    }

    void EnterCombat(Unit *who)
    {
        SummonAdvisor();
    }
    void UpdateAI(const uint32 diff)
    {
        if(!UpdateVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_flight_master(Creature *_Creature)
{
    return new npc_flight_masterAI(_Creature);
}

/*######
## npc_garments_of_quests
######*/

enum
{
    SPELL_LESSER_HEAL_R2    = 2052,
    SPELL_FORTITUDE_R1      = 1243,

    QUEST_MOON              = 5621,
    QUEST_LIGHT_1           = 5624,
    QUEST_LIGHT_2           = 5625,
    QUEST_SPIRIT            = 5648,
    QUEST_DARKNESS          = 5650,

    ENTRY_SHAYA             = 12429,
    ENTRY_ROBERTS           = 12423,
    ENTRY_DOLF              = 12427,
    ENTRY_KORJA             = 12430,
    ENTRY_DG_KEL            = 12428,

    SAY_COMMON_HEALED       = -1000531,
    SAY_DG_KEL_THANKS       = -1000532,
    SAY_DG_KEL_GOODBYE      = -1000533,
    SAY_ROBERTS_THANKS      = -1000556,
    SAY_ROBERTS_GOODBYE     = -1000557,
    SAY_KORJA_THANKS        = -1000558,
    SAY_KORJA_GOODBYE       = -1000559,
    SAY_DOLF_THANKS         = -1000560,
    SAY_DOLF_GOODBYE        = -1000561,
    SAY_SHAYA_THANKS        = -1000562,
    SAY_SHAYA_GOODBYE       = -1000563,
};

float RunTo[5][3]=
{
    {9661.724, 869.803, 1270.742},                          //shaya
    {-9543.747, -117.770, 57.893},                          //roberts
    {-5650.226, -473.517, 397.027},                         //dolf
    {189.175, -4747.069, 11.215},                           //kor'ja
    {2471.303, 371.101, 30.919},                            //kel
};

struct TRINITY_DLL_DECL npc_garments_of_questsAI : public ScriptedAI
{
    npc_garments_of_questsAI(Creature *c) : ScriptedAI(c) {Reset();}

    uint64 caster;

    bool IsHealed;
    bool CanRun;

    uint32 RunAwayTimer;

    void Reset()
    {
        caster = 0;
        IsHealed = false;
        CanRun = false;

        RunAwayTimer = 5000;

        m_creature->SetStandState(PLAYER_STATE_KNEEL);
        m_creature->SetHealth(int(m_creature->GetMaxHealth()*0.7));
        m_creature->SetVisibility(VISIBILITY_ON);
    }

    void EnterCombat(Unit *who) {}

    void SpellHit(Unit* pCaster, const SpellEntry *Spell)
    {
        if(Spell->Id == SPELL_LESSER_HEAL_R2 || Spell->Id == SPELL_FORTITUDE_R1)
        {
            //not while in combat
            if(m_creature->isInCombat())
                return;

            //nothing to be done now
            if(IsHealed && CanRun)
                return;

            if(pCaster->GetTypeId() == TYPEID_PLAYER)
            {
                switch(m_creature->GetEntry())
                {
                    case ENTRY_SHAYA:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_MOON) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (IsHealed && !CanRun && Spell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_SHAYA_THANKS,m_creature,pCaster);
                                CanRun = true;
                            }
                            else if (!IsHealed && Spell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                caster = pCaster->GetGUID();
                                m_creature->SetStandState(PLAYER_STATE_NONE);
                                DoScriptText(SAY_COMMON_HEALED,m_creature,pCaster);
                                IsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_ROBERTS:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_LIGHT_1) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (IsHealed && !CanRun && Spell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_ROBERTS_THANKS,m_creature,pCaster);
                                CanRun = true;
                            }
                            else if (!IsHealed && Spell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                caster = pCaster->GetGUID();
                                m_creature->SetStandState(PLAYER_STATE_NONE);
                                DoScriptText(SAY_COMMON_HEALED,m_creature,pCaster);
                                IsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_DOLF:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_LIGHT_2) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (IsHealed && !CanRun && Spell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_DOLF_THANKS,m_creature,pCaster);
                                CanRun = true;
                            }
                            else if (!IsHealed && Spell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                caster = pCaster->GetGUID();
                                m_creature->SetStandState(PLAYER_STATE_NONE);
                                DoScriptText(SAY_COMMON_HEALED,m_creature,pCaster);
                                IsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_KORJA:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_SPIRIT) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (IsHealed && !CanRun && Spell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_KORJA_THANKS,m_creature,pCaster);
                                CanRun = true;
                            }
                            else if (!IsHealed && Spell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                caster = pCaster->GetGUID();
                                m_creature->SetStandState(PLAYER_STATE_NONE);
                                DoScriptText(SAY_COMMON_HEALED,m_creature,pCaster);
                                IsHealed = true;
                            }
                        }
                        break;
                    case ENTRY_DG_KEL:
                        if (((Player*)pCaster)->GetQuestStatus(QUEST_DARKNESS) == QUEST_STATUS_INCOMPLETE)
                        {
                            if (IsHealed && !CanRun && Spell->Id == SPELL_FORTITUDE_R1)
                            {
                                DoScriptText(SAY_DG_KEL_THANKS,m_creature,pCaster);
                                CanRun = true;
                            }
                            else if (!IsHealed && Spell->Id == SPELL_LESSER_HEAL_R2)
                            {
                                caster = pCaster->GetGUID();
                                m_creature->SetStandState(PLAYER_STATE_NONE);
                                DoScriptText(SAY_COMMON_HEALED,m_creature,pCaster);
                                IsHealed = true;
                            }
                        }
                        break;
                }

                //give quest credit, not expect any special quest objectives
                if (CanRun)
                    ((Player*)pCaster)->TalkedToCreature(m_creature->GetEntry(),m_creature->GetGUID());
            }
        }
    }

    void MovementInform(uint32 type, uint32 id)
    {
        if (type != POINT_MOTION_TYPE)
            return;

        //we reached destination, kill ourselves
        if (id == 0)
        {
            m_creature->SetVisibility(VISIBILITY_OFF);
            m_creature->setDeathState(JUST_DIED);
            m_creature->SetHealth(0);
            m_creature->CombatStop();
            m_creature->DeleteThreatList();
            m_creature->RemoveCorpse();
        }
    }

    void UpdateAI(const uint32 diff)
    {
        if (CanRun && !m_creature->isInCombat())
        {
            if (RunAwayTimer <= diff)
            {
                if (Unit *pUnit = Unit::GetUnit(*m_creature,caster))
                {
                    switch(m_creature->GetEntry())
                    {
                        case ENTRY_SHAYA:
                            DoScriptText(SAY_SHAYA_GOODBYE,m_creature,pUnit);
                            m_creature->GetMotionMaster()->MovePoint(0, RunTo[0][0], RunTo[0][1], RunTo[0][2]);
                            break;
                        case ENTRY_ROBERTS:
                            DoScriptText(SAY_ROBERTS_GOODBYE,m_creature,pUnit);
                            m_creature->GetMotionMaster()->MovePoint(0, RunTo[1][0], RunTo[1][1], RunTo[1][2]);
                            break;
                        case ENTRY_DOLF:
                            DoScriptText(SAY_DOLF_GOODBYE,m_creature,pUnit);
                            m_creature->GetMotionMaster()->MovePoint(0, RunTo[2][0], RunTo[2][1], RunTo[2][2]);
                            break;
                        case ENTRY_KORJA:
                            DoScriptText(SAY_KORJA_GOODBYE,m_creature,pUnit);
                            m_creature->GetMotionMaster()->MovePoint(0, RunTo[3][0], RunTo[3][1], RunTo[3][2]);
                            break;
                        case ENTRY_DG_KEL:
                            DoScriptText(SAY_DG_KEL_GOODBYE,m_creature,pUnit);
                            m_creature->GetMotionMaster()->MovePoint(0, RunTo[4][0], RunTo[4][1], RunTo[4][2]);
                            break;
                    }
                }
                else
                    EnterEvadeMode();                       //something went wrong

                RunAwayTimer = 30000;
            }
            else
                RunAwayTimer -= diff;
        }

       DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_garments_of_quests(Creature* pCreature)
{
    return new npc_garments_of_questsAI(pCreature);
}

/*########
# npc_mojo
#########*/

#define SPELL_FEELING_FROGGY    43906
#define SPELL_HEARTS            20372   //wrong ?
#define MOJO_WHISPS_COUNT       8

struct TRINITY_DLL_DECL npc_mojoAI : public ScriptedAI
{
    npc_mojoAI(Creature *c) : ScriptedAI(c) {}

    uint32 heartsResetTimer;
    bool hearts;

    void Reset()
    {
        heartsResetTimer = 15000;
        hearts = false;
        m_creature->GetMotionMaster()->Clear();
        m_creature->GetMotionMaster()->MoveFollow(m_creature->GetOwner(), 2.0, M_PI/2);
    }

    void EnterCombat(Unit *who) {}

    void OnAuraApply(Aura* aur, Unit* caster, bool stackApply)
    {
        if (aur->GetId() == SPELL_HEARTS)
        {
            hearts = true;
            heartsResetTimer = 15000;
        }
    }

    void UpdateAI(const uint32 diff)
    {
        if (hearts)
        {
            if (heartsResetTimer <= diff)
            {
                m_creature->RemoveAurasDueToSpell(SPELL_HEARTS);
                hearts = false;
                m_creature->GetMotionMaster()->Clear(true);
                m_creature->GetMotionMaster()->MoveFollow(m_creature->GetOwner(), 2.0, M_PI/2);
                m_creature->SetSelection(NULL);
            }
            else
                heartsResetTimer -= diff;
        }
    }
};

bool ReceiveEmote_npc_mojo( Player *player, Creature *_Creature, uint32 emote )
{
    if( emote == TEXTEMOTE_KISS )
    {
        if (!_Creature->HasAura(SPELL_HEARTS, 0))
        {
            //affect only the same conflict side (horde -> horde or ally -> ally)
            if( player->GetTeam() == _Creature->GetCharmerOrOwnerPlayerOrPlayerItself()->GetTeam() )
            {
                player->CastSpell(player, SPELL_FEELING_FROGGY, false);
                _Creature->CastSpell(_Creature, SPELL_HEARTS, false);
                _Creature->SetSelection(player->GetGUID());
                _Creature->StopMoving();
                _Creature->GetMotionMaster()->Clear();
                _Creature->GetMotionMaster()->MoveFollow(player, 1.0, 0);

                char * text;

                switch (urand(0, MOJO_WHISPS_COUNT))
                {
                    case 0:
                        text = "Now that's what I call froggy-style!";
                        break;
                    case 1:
                        text = "Your lily pad or mine?";
                        break;
                    case 2:
                        text = "This won't take long, did it?";
                        break;
                    case 3:
                        text = "I thought you'd never ask!";
                        break;
                    case 4:
                        text = "I promise not to give you warts...";
                        break;
                    case 5:
                        text = "Feelin' a little froggy, are ya?";
                        break;
                    case 6:
                        text = "Listen, $n, I know of a little swamp not too far from here....";
                        break;
                    default:
                        text = "There's just never enough Mojo to go around...";
                        break;
                }

                _Creature->Whisper(text, player->GetGUID(), false);
            }
        }
    }

    return true;
}

CreatureAI* GetAI_npc_mojo(Creature *_Creature)
{
    return new npc_mojoAI(_Creature);
}


/*########
# npc_woeful_healer
#########*/

#define SPELL_PREYER_OF_HEALING     30604

struct TRINITY_DLL_DECL npc_woeful_healerAI : public ScriptedAI
{
    npc_woeful_healerAI(Creature *c) : ScriptedAI(c) {Reset();}

    uint32 healTimer;

    void Reset()
    {
        healTimer = urand(2500, 7500);
        m_creature->GetMotionMaster()->MoveFollow(m_creature->GetOwner(), 2.0, M_PI/2);
    }

    void EnterCombat(Unit *who) {}

    void UpdateAI(const uint32 diff)
    {
        Unit * owner = m_creature->GetCharmerOrOwner();

        if (healTimer <= diff)
        {
            healTimer = urand(2500, 7500);
            if (!owner || !owner->isInCombat())
                return;
            m_creature->CastSpell(m_creature, SPELL_PREYER_OF_HEALING, false);
        }
        else
            healTimer -= diff;
    }
};

CreatureAI* GetAI_npc_woeful_healer(Creature* pCreature)
{
    return new npc_woeful_healerAI(pCreature);
}

#define GOSSIP_VIOLET_SIGNET    "I have lost my Violet Signet, could you make me a new one?"
#define GOSSIP_ETERNAL_BAND    "I have lost my Eternal Band, could you make me a new one?"

// Archmage Leryda from Karazhan and Soridormi from Mount Hyjal
bool GossipHello_npc_ring_specialist(Player* player, Creature* _Creature)
{
    if (_Creature->isQuestGiver())
        player->PrepareQuestMenu( _Creature->GetGUID() );
    uint32 entry = _Creature->GetEntry();
    switch(entry)
    {
        // Archmage Leryda
        case 18253:
            // player has none of the rings
            if((!player->HasItemCount(29287, 1, true) && !player->HasItemCount(29279, 1, true) && !player->HasItemCount(29283, 1, true) && !player->HasItemCount(29290, 1, true))
                && // and had completed one of the chains
               (player->GetQuestRewardStatus(10725) || player->GetQuestRewardStatus(10728) || player->GetQuestRewardStatus(10727) || player->GetQuestRewardStatus(10726)))
                    player->ADD_GOSSIP_ITEM( 0, GOSSIP_VIOLET_SIGNET, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
            break;
        // Soridormi
        case 19935:
            // player has none of the rings
            if((!player->HasItemCount(29305, 1, true) && !player->HasItemCount(29309, 1, true) && !player->HasItemCount(29301, 1, true) && !player->HasItemCount(29297, 1, true))
                && // and had completed one of the chains
               (player->GetQuestRewardStatus(10472) || player->GetQuestRewardStatus(10473) || player->GetQuestRewardStatus(10474) || player->GetQuestRewardStatus(10475)))
                    player->ADD_GOSSIP_ITEM( 0, GOSSIP_ETERNAL_BAND,   GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2);
            break;
    }

    player->SEND_GOSSIP_MENU(_Creature->GetNpcTextId(), _Creature->GetGUID());
    return true;
}

void RestoreQuestRingItem(Player* player, uint32 id)
{
        ItemPosCountVec dest;
        uint8 msg = player->CanStoreNewItem( NULL_BAG, NULL_SLOT, dest, id, 1);
        if( msg == EQUIP_ERR_OK )
        {
            Item* item = player->StoreNewItem( dest, id, true);
            player->SendNewItem(item, 1, true, false);
        }
}

bool GossipSelect_npc_ring_specialist(Player* player, Creature* _Creature, uint32 sender, uint32 action)
{
    switch( action )
    {
        case GOSSIP_ACTION_INFO_DEF + 1:
            if(player->GetQuestRewardStatus(10725))
                RestoreQuestRingItem(player, 29287);
            if(player->GetQuestRewardStatus(10728))
                RestoreQuestRingItem(player, 29279);
            if(player->GetQuestRewardStatus(10727))
                RestoreQuestRingItem(player, 29283);
            if(player->GetQuestRewardStatus(10726))
                RestoreQuestRingItem(player, 29290);
            player->CLOSE_GOSSIP_MENU();
            break;
        case GOSSIP_ACTION_INFO_DEF + 2:
            if(player->GetQuestRewardStatus(10472))
                RestoreQuestRingItem(player, 29305);
            if(player->GetQuestRewardStatus(10473))
                RestoreQuestRingItem(player, 29309);
            if(player->GetQuestRewardStatus(10474))
                RestoreQuestRingItem(player, 29301);
            if(player->GetQuestRewardStatus(10475))
                RestoreQuestRingItem(player, 29297);
            player->CLOSE_GOSSIP_MENU();
            break;
    }
    return true;
}

/*########
# npc_elemental_guardian
#########*/

struct TRINITY_DLL_DECL npc_elemental_guardianAI : public ScriptedAI
{
    npc_elemental_guardianAI(Creature *c) : ScriptedAI(c){}

    uint32 m_checkTimer;

    void Reset()
    {
        m_checkTimer = 2000;
    }

    void Despawn()
    {
        m_creature->Kill(m_creature, false);
        m_creature->RemoveCorpse();
    }

    void EnterCombat(Unit *who){}

    void MoveInLineOfSight(Unit *pWho)
    {
        if(pWho->GetTypeId() == TYPEID_PLAYER && pWho->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY))
            return;

        if(!m_creature->getVictim() && m_creature->IsHostileTo(pWho))
        {
            Creature *pTotem = m_creature->GetCreature(*m_creature, m_creature->GetOwnerGUID());
            if(pTotem && pTotem->IsWithinDistInMap(pWho, 30.0f))
                AttackStart(pWho);
        }
    }

    void UpdateAI(const uint32 diff)
    {
        if(m_checkTimer < diff)
        {
            Creature *pTotem = m_creature->GetCreature(*m_creature, m_creature->GetOwnerGUID());
            
            if(!me->getVictim() && pTotem)
            {
                if(!m_creature->hasUnitState(UNIT_STAT_FOLLOW))
                    m_creature->GetMotionMaster()->MoveFollow(pTotem, 2.0f, M_PI);

                if(Unit *pOwner = pTotem->GetOwner()) 
                {
                    if(pOwner->GetTypeId() != TYPEID_PLAYER || !pOwner->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_SANCTUARY))
                    {
                        if(Unit *pTemp = pTotem->SelectNearestTarget(30.0))
                            AttackStart(pTemp);
                    }
                }
            }

            if(pTotem)
            {
                if(!pTotem->isAlive())
                {
                    Despawn();
                    return;
                }

                if(!m_creature->IsWithinDistInMap(pTotem, 30.0f))
                {
                    EnterEvadeMode();
                    m_creature->GetMotionMaster()->MoveFollow(pTotem, 2.0f, M_PI);
                }
            }
            else
                Despawn();

            m_checkTimer = 2000;
        }
        else
            m_checkTimer -= diff;

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_npc_elemental_guardian(Creature* pCreature)
{
    return new npc_elemental_guardianAI(pCreature);
}

/*########
# npc_master_omarion
#########*/

//Blacksmithing
#define GOSSIP_ITEM_OMARION0  "Learn Icebane Bracers pattern."
#define GOSSIP_ITEM_OMARION1  "Learn Icebane Gauntlets pattern."
#define GOSSIP_ITEM_OMARION2  "Learn Icebane Breastplate pattern."
//Leatherworking
#define GOSSIP_ITEM_OMARION3  "Learn Polar Bracers pattern."
#define GOSSIP_ITEM_OMARION4  "Learn Polar Gloves pattern."
#define GOSSIP_ITEM_OMARION5  "Learn Polar Tunic pattern."
#define GOSSIP_ITEM_OMARION6  "Learn Icy Scale Bracers pattern."
#define GOSSIP_ITEM_OMARION7  "Learn Icy Scale Gauntlets pattern."
#define GOSSIP_ITEM_OMARION8  "Learn Icy Scale Breastplate pattern."
//Tailoring
#define GOSSIP_ITEM_OMARION9  "Learn Glacial Wrists pattern."
#define GOSSIP_ITEM_OMARION10 "Learn Glacial Gloves pattern."
#define GOSSIP_ITEM_OMARION11 "Learn Glacial Vest pattern."
#define GOSSIP_ITEM_OMARION12 "Learn Glacial Cloak pattern."

bool GossipHello_npc_master_omarion(Player *player, Creature *_Creature)
{
bool isexalted,isrevered;
isexalted = false;
isrevered = false;
  if(player->GetReputation(529) >= 21000)
  {
    isrevered = true;
    if(player->GetReputation(529) >= 42000)
      isexalted = true;
  }

  if(player->GetBaseSkillValue(SKILL_BLACKSMITHING)>=300) // Blacksmithing +300
  {
    if(isrevered)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION0    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1);
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION1    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 2); 
    if(isexalted)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION2    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 3);
    }
    }
  }
  if(player->GetBaseSkillValue(SKILL_LEATHERWORKING)>=300) // Leatherworking +300
  {
    if(isrevered)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION3    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 4);
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION4    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 5);
    if(isexalted)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION5    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 6);
    }
    }
    if(isrevered)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION6    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 7);
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION7    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 8);
    if(isexalted)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION8    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 9);
    }
    }
  }
  if(player->GetBaseSkillValue(SKILL_TAILORING)>=300) // Tailoring +300
  {
    if(isrevered)
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION9    , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 10);
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION10   , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 11);
    if(isexalted) 
    {
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION11   , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 12);
      player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_OMARION12   , GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 13);
    }
    }
  }
player->SEND_GOSSIP_MENU(_Creature->GetNpcTextId(), _Creature->GetGUID());
return true;
}

bool GossipSelect_npc_master_omarion(Player *player, Creature *_Creature, uint32 sender, uint32 action )
{
switch (action)
{    
      case GOSSIP_ACTION_INFO_DEF + 1:         // Icebane Bracers
        player->learnSpell( 28244 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 2:         // Icebane Gauntlets
        player->learnSpell( 28243 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 3:         // Icebane Breastplate
        player->learnSpell( 28242 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 4:         // Polar Bracers
        player->learnSpell( 28221 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 5:         // Polar Gloves
        player->learnSpell( 28220 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 6:         // Polar Tunic
        player->learnSpell( 28219 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 7:         // Icy Scale Bracers
        player->learnSpell( 28224 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 8:         // Icy Scale Gauntlets
        player->learnSpell( 28223 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 9:         // Icy Scale Breastplate
        player->learnSpell( 28222 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 10:        // Glacial Wrists
        player->learnSpell( 28209 );
        break; 
      case GOSSIP_ACTION_INFO_DEF + 11:        // Glacial Gloves
        player->learnSpell( 28205 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 12:        // Glacial Vest
        player->learnSpell( 28207 );
        break;
      case GOSSIP_ACTION_INFO_DEF + 13:        // Glacial Cloak
        player->learnSpell( 28208 );
        break;
    }
    player->CLOSE_GOSSIP_MENU();
    return true;
}


/*########
# npc_lorekeeper_lydros
#########*/

#define GOSSIP_ITEM_LOREKEEPER1 "Fascinating, Lorekeeper. Continue please."
#define GOSSIP_ITEM_LOREKEEPER2 "(Continue)"
#define GOSSIP_ITEM_LOREKEEPER3 "Eh?"
#define GOSSIP_ITEM_LOREKEEPER4 "Maybe... What do I do now?"

bool GossipHello_npc_lorekeeper_lydros(Player *player, Creature *_Creature)
{
    if (_Creature->isQuestGiver())
        player->PrepareQuestMenu( _Creature->GetGUID() );
    if(player->GetQuestRewardStatus(7507) && !player->HasItemCount(18513,1))
        player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_LOREKEEPER1, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF + 1 );
		
player->SEND_GOSSIP_MENU(24999, _Creature->GetGUID());
return true;
}

bool GossipSelect_npc_lorekeeper_lydros(Player *player, Creature *_Creature, uint32 sender, uint32 action )
{
  switch (action)
    {
        case GOSSIP_ACTION_INFO_DEF+1:
            player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_LOREKEEPER2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+2);
            player->SEND_GOSSIP_MENU(25000, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+2:
            player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_LOREKEEPER2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+3);
            player->SEND_GOSSIP_MENU(25001, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+3:
            player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_LOREKEEPER2, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+4);
            player->SEND_GOSSIP_MENU(25002, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+4:
            player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_LOREKEEPER3, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+5);
            player->SEND_GOSSIP_MENU(25003, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+5:
            player->ADD_GOSSIP_ITEM( 0, GOSSIP_ITEM_LOREKEEPER4, GOSSIP_SENDER_MAIN, GOSSIP_ACTION_INFO_DEF+6);
            player->SEND_GOSSIP_MENU(25004, _Creature->GetGUID());
            break;
        case GOSSIP_ACTION_INFO_DEF+6:
            ItemPosCountVec dest;
            uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, 18513, 1);
            if (msg == EQUIP_ERR_OK)
            {
                 Item* item = player->StoreNewItem(dest, 18513, true);
                 player->SendNewItem(item,1,true,false,true);
            }
            player->CLOSE_GOSSIP_MENU();
            break;
    }
    return true;
}

void AddSC_npcs_special()
{
    Script *newscript;

    newscript = new Script;
    newscript->Name = "npc_garments_of_quests";
    newscript->GetAI = &GetAI_npc_garments_of_quests;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_chicken_cluck";
    newscript->GetAI = &GetAI_npc_chicken_cluck;
    newscript->pReceiveEmote =  &ReceiveEmote_npc_chicken_cluck;
    newscript->pQuestAccept =   &QuestAccept_npc_chicken_cluck;
    newscript->pQuestComplete = &QuestComplete_npc_chicken_cluck;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_dancing_flames";
    newscript->GetAI = &GetAI_npc_dancing_flames;
    newscript->pReceiveEmote =  &ReceiveEmote_npc_dancing_flames;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_injured_patient";
    newscript->GetAI = &GetAI_npc_injured_patient;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_doctor";
    newscript->GetAI = &GetAI_npc_doctor;
    newscript->pQuestAccept = &QuestAccept_npc_doctor;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_guardian";
    newscript->GetAI = &GetAI_npc_guardian;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_mount_vendor";
    newscript->pGossipHello =  &GossipHello_npc_mount_vendor;
    newscript->pGossipSelect = &GossipSelect_npc_mount_vendor;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_rogue_trainer";
    newscript->pGossipHello =  &GossipHello_npc_rogue_trainer;
    newscript->pGossipSelect = &GossipSelect_npc_rogue_trainer;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_sayge";
    newscript->pGossipHello = &GossipHello_npc_sayge;
    newscript->pGossipSelect = &GossipSelect_npc_sayge;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_steam_tonk";
    newscript->GetAI = &GetAI_npc_steam_tonk;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_tonk_mine";
    newscript->GetAI = &GetAI_npc_tonk_mine;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_winter_reveler";
    newscript->pReceiveEmote =  &ReceiveEmote_npc_winter_reveler;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_brewfest_reveler";
    newscript->pReceiveEmote =  &ReceiveEmote_npc_brewfest_reveler;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_snake_trap_serpents";
    newscript->GetAI = &GetAI_npc_snake_trap_serpents;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_flight_master";
    newscript->GetAI = &GetAI_npc_flight_master;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_mojo";
    newscript->GetAI = &GetAI_npc_mojo;
    newscript->pReceiveEmote =  &ReceiveEmote_npc_mojo;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_woeful_healer";
    newscript->GetAI = &GetAI_npc_woeful_healer;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_ring_specialist";
    newscript->pGossipHello = &GossipHello_npc_ring_specialist;
    newscript->pGossipSelect = &GossipSelect_npc_ring_specialist;
    newscript->RegisterSelf();

    newscript = new Script;
    newscript->Name="npc_elemental_guardian";
    newscript->GetAI = &GetAI_npc_elemental_guardian;
    newscript->RegisterSelf();
    
    newscript = new Script;
    newscript->Name="npc_master_omarion";
    newscript->pGossipHello =  &GossipHello_npc_master_omarion;
    newscript->pGossipSelect = &GossipSelect_npc_master_omarion;
    newscript->RegisterSelf();
    
    newscript = new Script;
    newscript->Name="npc_lorekeeper_lydros";
    newscript->pGossipHello =  &GossipHello_npc_lorekeeper_lydros;
    newscript->pGossipSelect = &GossipSelect_npc_lorekeeper_lydros;
    newscript->RegisterSelf();
}

