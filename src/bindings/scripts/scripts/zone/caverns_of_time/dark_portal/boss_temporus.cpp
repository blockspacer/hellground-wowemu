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
SDName: Boss_Temporus
SD%Complete: 95
SDComment: Some timers may need to be adjusted
SDCategory: Caverns of Time, The Dark Portal
EndScriptData */

#include "precompiled.h"
#include "def_dark_portal.h"

#define SAY_ENTER               -1269000
#define SAY_AGGRO               -1269001
#define SAY_BANISH              -1269002
#define SAY_SLAY1               -1269003
#define SAY_SLAY2               -1269004
#define SAY_DEATH               -1269005

#define SPELL_HASTE             31458
#define SPELL_MORTAL_WOUND      31464
#define SPELL_WING_BUFFET       31475
#define H_SPELL_WING_BUFFET     38593
#define SPELL_REFLECT           38592

struct TRINITY_DLL_DECL boss_temporusAI : public ScriptedAI
{
    boss_temporusAI(Creature *c) : ScriptedAI(c)
    {
        pInstance = ((ScriptedInstance*)c->GetInstanceData());
        HeroicMode = m_creature->GetMap()->IsHeroic();
    }

    ScriptedInstance *pInstance;
    bool HeroicMode;
    bool canApplyWound;

    uint32 MortalWound_Timer;
    uint32 WingBuffet_Timer;
    uint32 Haste_Timer;
    uint32 SpellReflection_Timer;

    void Reset()
    {
        m_creature->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_TAUNT, true);
        m_creature->ApplySpellImmune(0, IMMUNITY_EFFECT,SPELL_EFFECT_ATTACK_ME, true);

        MortalWound_Timer = 5000;
        canApplyWound = false;
        WingBuffet_Timer = 10000;
        Haste_Timer = 20000;
        SpellReflection_Timer = 40000;
        m_creature->setActive(true);

        SayIntro();
    }

        void SayIntro()
    {
        DoScriptText(SAY_ENTER, m_creature);
    }

    void Aggro(Unit *who)
    {
        DoScriptText(SAY_AGGRO, m_creature);
    }

    void KilledUnit(Unit *victim)
    {
        switch(rand()%2)
        {
            case 0: DoScriptText(SAY_SLAY1, m_creature); break;
            case 1: DoScriptText(SAY_SLAY2, m_creature); break;
        }
    }

    void JustDied(Unit *victim)
    {
        if(pInstance)
        {
            if(pInstance->GetData(TYPE_MEDIVH) != FAIL)
                DoScriptText(SAY_DEATH, m_creature);

            pInstance->SetData(TYPE_RIFT,SPECIAL);
            pInstance->SetData(TYPE_TEMPORUS,DONE);
        }
    }

    void MoveInLineOfSight(Unit *who)
    {
        //Despawn Time Keeper
        if (who->GetTypeId() == TYPEID_UNIT && who->GetEntry() == C_TIME_KEEPER)
        {
            if (m_creature->IsWithinDistInMap(who,20.0f))
            {
                DoScriptText(SAY_BANISH, m_creature);

                m_creature->DealDamage(who, who->GetHealth(), DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, NULL, false);
            }
        }

        ScriptedAI::MoveInLineOfSight(who);
    }

    void DamageMade(Unit* target, uint32 & damage, bool direct_damage) 
    {
        if(canApplyWound)
            DoCast(target, SPELL_MORTAL_WOUND);

        canApplyWound = false;
    }

    void UpdateAI(const uint32 diff)
    {
        //Return since we have no target
        if (!UpdateVictim())
            return;

        //Attack Haste
        if (Haste_Timer < diff)
        {
            DoCast(m_creature, SPELL_HASTE);
            Haste_Timer = 20000+rand()%5000;
        }else Haste_Timer -= diff;

        //Wing Buffet
        if (WingBuffet_Timer < diff)
        {
            if(HeroicMode)
                DoCast(m_creature, H_SPELL_WING_BUFFET);
            else
                DoCast(m_creature, SPELL_WING_BUFFET);
            WingBuffet_Timer = 15000+rand()%10000;
        }
        else
            WingBuffet_Timer -= diff;

        //Mortal Wound
        if (MortalWound_Timer < diff)
        {
            canApplyWound = true;

            if(m_creature->HasAura(SPELL_HASTE, 0))
                MortalWound_Timer = 2000+rand()%1000;
            else
                MortalWound_Timer = 6000+rand()%3000;
        }
        else
            MortalWound_Timer -= diff;

        //Spell Reflection
        if (HeroicMode && SpellReflection_Timer < diff)
        {
            DoCast(m_creature, SPELL_REFLECT);
            SpellReflection_Timer = 40000+rand()%10000;
        }else SpellReflection_Timer -= diff;

        //if event failed, remove boss from instance
        if(pInstance && pInstance->GetData(TYPE_MEDIVH) == FAIL)
        {
            m_creature->Kill(m_creature, false);
            m_creature->RemoveCorpse();
        }

        DoMeleeAttackIfReady();
    }
};

CreatureAI* GetAI_boss_temporus(Creature *_Creature)
{
    return new boss_temporusAI (_Creature);
}

void AddSC_boss_temporus()
{
    Script *newscript;
    newscript = new Script;
    newscript->Name="boss_temporus";
    newscript->GetAI = &GetAI_boss_temporus;
    newscript->RegisterSelf();
}

