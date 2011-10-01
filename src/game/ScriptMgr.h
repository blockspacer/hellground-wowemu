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

#ifndef _SCRIPTMGR_H
#define _SCRIPTMGR_H

#include "Common.h"
#include "Policies/Singleton.h"

struct AreaTriggerEntry;
class Aura;
class Creature;
class CreatureAI;
class GameObject;
class InstanceData;
class Item;
class Map;
class Object;
class Player;
class Quest;
class SpellCastTargets;
class Unit;
class WorldObject;

#define MIN_DB_SCRIPT_STRING_ID        MAX_TRINITY_STRING_ID // 'db_script_string'
#define MAX_DB_SCRIPT_STRING_ID        2000010000

struct ScriptInfo
{
    uint32 id;
    uint32 delay;
    uint32 command;
    uint32 datalong;
    uint32 datalong2;
    int32  dataint;
    float x;
    float y;
    float z;
    float o;
};

typedef std::multimap<uint32, ScriptInfo> ScriptMap;
typedef std::map<uint32, ScriptMap > ScriptMapMap;
extern ScriptMapMap sQuestEndScripts;
extern ScriptMapMap sQuestStartScripts;
extern ScriptMapMap sSpellScripts;
extern ScriptMapMap sGameObjectScripts;
extern ScriptMapMap sEventScripts;
extern ScriptMapMap sWaypointScripts;

class ScriptMgr
{
        typedef std::vector<std::string> ScriptNameMap;
    public:
        ScriptMgr();
        ~ScriptMgr();

        void LoadGameObjectScripts();
        void LoadQuestEndScripts();
        void LoadQuestStartScripts();
        void LoadEventScripts();
        void LoadEventIdScripts();
        void LoadSpellScripts();
        void LoadWaypointScripts();

        void LoadDbScriptStrings();

        void LoadScriptNames();
        void LoadAreaTriggerScripts();


        uint32 GetAreaTriggerScriptId(uint32 triggerId) const;
        uint32 GetEventIdScriptId(uint32 eventId) const;

        ScriptNameMap &GetScriptNames() { return m_scriptNames; }
        const char * GetScriptName(uint32 id) { return id < m_scriptNames.size() ? m_scriptNames[id].c_str() : ""; }
        uint32 GetScriptId(const char *name);

        bool LoadScriptLibrary(const char* libName);
        void UnloadScriptLibrary();

        CreatureAI* GetCreatureAI(Creature* pCreature);
        InstanceData* CreateInstanceData(Map* pMap);

        bool OnGossipHello(Player* pPlayer, Creature* pCreature);
        bool OnGossipHello(Player* pPlayer, GameObject* pGameObject);
        bool OnGossipSelect(Player* pPlayer, Creature* pCreature, uint32 sender, uint32 action, const char* code);
        bool OnGossipSelect(Player* pPlayer, GameObject* pGameObject, uint32 sender, uint32 action, const char* code);
        bool OnQuestAccept(Player* pPlayer, Creature* pCreature, Quest const* pQuest);
        bool OnQuestAccept(Player* pPlayer, GameObject* pGameObject, Quest const* pQuest);
        bool OnQuestAccept(Player* pPlayer, Item* pItem, Quest const* pQuest);
        bool OnQuestRewarded(Player* pPlayer, Creature* pCreature, Quest const* pQuest);
        bool OnQuestRewarded(Player* pPlayer, GameObject* pGameObject, Quest const* pQuest);
        uint32 GetDialogStatus(Player* pPlayer, Creature* pCreature);
        uint32 GetDialogStatus(Player* pPlayer, GameObject* pGameObject);
        bool OnGameObjectUse(Player* pPlayer, GameObject* pGameObject);
        bool OnItemUse(Player* pPlayer, Item* pItem, SpellCastTargets const& targets);
        bool OnAreaTrigger(Player* pPlayer, AreaTriggerEntry const* atEntry);
        bool OnProcessEvent(uint32 eventId, Object* pSource, Object* pTarget, bool isStart);
        bool OnEffectDummy(Unit* pCaster, uint32 spellId, uint32 effIndex, Creature* pTarget);
        bool OnEffectDummy(Unit* pCaster, uint32 spellId, uint32 effIndex, GameObject* pTarget);
        bool OnEffectDummy(Unit* pCaster, uint32 spellId, uint32 effIndex, Item* pTarget);
        bool OnAuraDummy(Aura const* pAura, bool apply);

        bool OnReceiveEmote(Player *pPlayer, Creature *pCreature, uint32 emote);

    private:
        void LoadScripts(ScriptMapMap& scripts, const char* tablename);
        void CheckScripts(ScriptMapMap const& scripts,std::set<int32>& ids);

        template<class T>
        void GetScriptHookPtr(T& ptr, const char* name)
        {
            ptr = (T)TRINITY_GET_PROC_ADDR(m_hScriptLib, name);
        }

        typedef UNORDERED_MAP<uint32, uint32> AreaTriggerScriptMap;
        typedef UNORDERED_MAP<uint32, uint32> EventIdScriptMap;

        AreaTriggerScriptMap    m_AreaTriggerScripts;
        EventIdScriptMap        m_EventIdScripts;

        ScriptNameMap           m_scriptNames;

        TRINITY_LIBRARY_HANDLE   m_hScriptLib;

        void (TRINITY_IMPORT* m_pOnInitScriptLibrary)();
        void (TRINITY_IMPORT* m_pOnFreeScriptLibrary)();
        const char* (TRINITY_IMPORT* m_pGetScriptLibraryVersion)();

        CreatureAI* (TRINITY_IMPORT* m_pGetCreatureAI) (Creature*);
        InstanceData* (TRINITY_IMPORT* m_pCreateInstanceData) (Map*);

        bool (TRINITY_IMPORT* m_pOnGossipHello) (Player*, Creature*);
        bool (TRINITY_IMPORT* m_pOnGOGossipHello) (Player*, GameObject*);
        bool (TRINITY_IMPORT* m_pOnGossipSelect) (Player*, Creature*, uint32, uint32);
        bool (TRINITY_IMPORT* m_pOnGOGossipSelect) (Player*, GameObject*, uint32, uint32);
        bool (TRINITY_IMPORT* m_pOnGossipSelectWithCode) (Player*, Creature*, uint32, uint32, const char*);
        bool (TRINITY_IMPORT* m_pOnGOGossipSelectWithCode) (Player*, GameObject*, uint32, uint32, const char*);
        bool (TRINITY_IMPORT* m_pOnQuestAccept) (Player*, Creature*, Quest const*);
        bool (TRINITY_IMPORT* m_pOnGOQuestAccept) (Player*, GameObject*, Quest const*);
        bool (TRINITY_IMPORT* m_pOnItemQuestAccept) (Player*, Item*, Quest const*);
        bool (TRINITY_IMPORT* m_pOnQuestRewarded) (Player*, Creature*, Quest const*);
        bool (TRINITY_IMPORT* m_pOnGOQuestRewarded) (Player*, GameObject*, Quest const*);
        uint32 (TRINITY_IMPORT* m_pGetNPCDialogStatus) (Player*, Creature*);
        uint32 (TRINITY_IMPORT* m_pGetGODialogStatus) (Player*, GameObject*);
        bool (TRINITY_IMPORT* m_pOnGOUse) (Player*, GameObject*);
        bool (TRINITY_IMPORT* m_pOnItemUse) (Player*, Item*, SpellCastTargets const&);
        bool (TRINITY_IMPORT* m_pOnAreaTrigger) (Player*, AreaTriggerEntry const*);
        bool (TRINITY_IMPORT* m_pOnProcessEvent) (uint32, Object*, Object*, bool);
        bool (TRINITY_IMPORT* m_pOnEffectDummyCreature) (Unit*, uint32, uint32, Creature*);
        bool (TRINITY_IMPORT* m_pOnEffectDummyGO) (Unit*, uint32, uint32, GameObject*);
        bool (TRINITY_IMPORT* m_pOnEffectDummyItem) (Unit*, uint32, uint32, Item*);
        bool (TRINITY_IMPORT* m_pOnAuraDummy) (Aura const*, bool);

        bool (TRINITY_IMPORT* m_pOnReceiveEmote) (Player *pPlayer, Creature *pCreature, uint32 emote);
};

#define sScriptMgr Trinity::Singleton<ScriptMgr>::Instance()

TRINITY_DLL_SPEC uint32 GetAreaTriggerScriptId(uint32 triggerId);
TRINITY_DLL_SPEC uint32 GetScriptId(const char *name);
TRINITY_DLL_SPEC uint32 GetEventIdScriptId(uint32 eventId);

#endif
