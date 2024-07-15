/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Battleground.h"
#include "BattlegroundAV.h"
#include "GameObjectAI.h"
#include "Group.h"
#include "Language.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "Config.h"
#include "Chat.h"
#include "ReputationMgr.h"

void WorldSession::HandleQuestgiverStatusQueryOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;
    uint32 questStatus = DIALOG_STATUS_NONE;

    GossipMenu& gossipMenu = _player->PlayerTalkClass->GetGossipMenu();
    // Did we already get a gossip menu with that NPC? if so no need to status query
    if (gossipMenu.GetSenderGUID() == guid)
    {
        return;
    }

    Object* questGiver = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!questGiver)
    {
        LOG_DEBUG("network.opcode", "Error in CMSG_QUESTGIVER_STATUS_QUERY, called for not found questgiver ({})", guid.ToString());
        return;
    }

    switch (questGiver->GetTypeId())
    {
        case TYPEID_UNIT:
            {
                LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_QUERY for npc {}", guid.ToString());
                if (!questGiver->ToCreature()->IsHostileTo(_player)) // do not show quest status to enemies
                    questStatus = _player->GetQuestDialogStatus(questGiver);
                break;
            }
        case TYPEID_GAMEOBJECT:
            {
                LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_STATUS_QUERY for GameObject {}", guid.ToString());
                if (sWorld->getBoolConfig(CONFIG_OBJECT_QUEST_MARKERS))
                {
                    questStatus = _player->GetQuestDialogStatus(questGiver);
                }
                break;
            }
        default:
            LOG_ERROR("network.opcode", "QuestGiver called for unexpected type {}", questGiver->GetTypeId());
            break;
    }

    // inform client about status of quest
    _player->PlayerTalkClass->SendQuestGiverStatus(uint8(questStatus), guid);
}

void WorldSession::HandleQuestgiverHelloOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    recvData >> guid;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_HELLO npc {}", guid.ToString());

    Creature* creature = GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE);
    if (!creature)
    {
        LOG_DEBUG("network", "WORLD: HandleQuestgiverHelloOpcode - Unit ({}) not found or you can't interact with him.", guid.ToString());
        return;
    }

    // remove fake death
    if (GetPlayer()->HasUnitState(UNIT_STATE_DIED))
        GetPlayer()->RemoveAurasByType(SPELL_AURA_FEIGN_DEATH);

    // Stop the npc if moving
    if (uint32 pause = creature->GetMovementTemplate().GetInteractionPauseTimer())
        creature->PauseMovement(pause);
    creature->SetHomePosition(creature->GetPosition());

    if (sScriptMgr->OnGossipHello(_player, creature))
        return;

    _player->PrepareGossipMenu(creature, creature->GetCreatureTemplate()->GossipMenuId, true);
    _player->SendPreparedGossip(creature);

    creature->AI()->sGossipHello(_player);
}

void WorldSession::HandleQuestgiverAcceptQuestOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 questId;
    uint32 unk1;
    recvData >> guid >> questId >> unk1;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_ACCEPT_QUEST npc {}, quest = {}, unk1 = {}", guid.ToString(), questId, unk1);

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM | TYPEMASK_PLAYER);

    // no or incorrect quest giver
    if (!object || object == _player || (object->GetTypeId() != TYPEID_PLAYER && !object->hasQuest(questId)) ||
            (object->GetTypeId() == TYPEID_PLAYER && !object->ToPlayer()->CanShareQuest(questId)))
    {
        _player->PlayerTalkClass->SendCloseGossip();
        _player->SetDivider();
        return;
    }

    // some kind of WPE protection
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        // pussywizard: exploit fix, can't share quests that give items to be sold
        if (object->GetTypeId() == TYPEID_PLAYER)
            if (uint32 itemId = quest->GetSrcItemId())
                if (ItemTemplate const* srcItem = sObjectMgr->GetItemTemplate(itemId))
                    if (srcItem->SellPrice > 0)
                        return;

        // prevent cheating
        if (!GetPlayer()->CanTakeQuest(quest, true))
        {
            _player->PlayerTalkClass->SendCloseGossip();
            _player->SetDivider();
            return;
        }

        if (_player->GetDivider())
        {
            Player* player = ObjectAccessor::GetPlayer(*_player, _player->GetDivider());
            if (player)
            {
                player->SendPushToPartyResponse(_player, QUEST_PARTY_MSG_ACCEPT_QUEST);
                _player->SetDivider();
            }
        }

        if (_player->CanAddQuest(quest, true))
        {
            _player->AddQuestAndCheckCompletion(quest, object);

            if (quest->HasFlag(QUEST_FLAGS_PARTY_ACCEPT))
            {
                if (Group* group = _player->GetGroup())
                {
                    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
                    {
                        Player* itrPlayer = itr->GetSource();
                        if (!itrPlayer || itrPlayer == _player || !itrPlayer->IsAtGroupRewardDistance(_player) || itrPlayer->HasPendingBind()) // xinef: check range
                            continue;

                        if (itrPlayer->CanTakeQuest(quest, false))
                        {
                            itrPlayer->SetDivider(_player->GetGUID());

                            // need confirmation that any gossip window will close
                            itrPlayer->PlayerTalkClass->SendCloseGossip();

                            _player->SendQuestConfirmAccept(quest, itrPlayer);
                        }
                    }
                }
            }

            _player->PlayerTalkClass->SendCloseGossip();

            if (quest->GetSrcSpell() > 0)
                _player->CastSpell(_player, quest->GetSrcSpell(), true);

            HandleQuestDetails(_player, quest); //直接完成配置列表的任务

            return;
        }
    }

    _player->PlayerTalkClass->SendCloseGossip();
}

void WorldSession::HandleQuestgiverQueryQuestOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;
    uint32 questId;
    uint8 unk1;
    recvData >> guid >> questId >> unk1;
    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_QUERY_QUEST npc {}, quest = {}, unk1 = {}", guid.ToString(), questId, unk1);

    // Verify that the guid is valid and is a questgiver or involved in the requested quest
    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT | TYPEMASK_ITEM);
    if (!object || (!object->hasQuest(questId) && !object->hasInvolvedQuest(questId)))
    {
        _player->PlayerTalkClass->SendCloseGossip();
        return;
    }

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        // not sure here what should happen to quests with QUEST_FLAGS_AUTOCOMPLETE
        // if this breaks them, add && object->GetTypeId() == TYPEID_ITEM to this check
        // item-started quests never have that flag
        if (!_player->CanTakeQuest(quest, true))
            return;

        if (quest->IsAutoAccept() && _player->CanAddQuest(quest, true))
            _player->AddQuestAndCheckCompletion(quest, object);

        if (quest->IsAutoComplete() || !quest->GetQuestMethod())
        {
            _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, object->GetGUID(), _player->CanCompleteQuest(quest->GetQuestId()), true);
        }
        else
        {
             _player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, object->GetGUID(), true); 
        }
    }
}

void WorldSession::HandleQuestDetails(Player* player, Quest const* quest)
{
    // 读取配置文件中的自动完成任务ID
    std::set<uint32> questIds;
    std::string questIdsStr = sConfigMgr->GetOption<std::string>("AutoCompleteQuests", "");
    
    std::istringstream iss(questIdsStr);
    std::string id;
    while (std::getline(iss, id, ','))
    {
        if (!id.empty())
        {
            questIds.insert(std::stoi(id));
        }
    }

    // 判断当前任务ID是否在自动完成任务ID列表中
    if (questIds.find(quest->GetQuestId()) != questIds.end())
    {
        // 自动完成任务
        ChatHandler handler(player->GetSession());
     

        if (player)
        {
            // If player doesn't have the quest
            if (player->GetQuestStatus(quest->GetQuestId()) == QUEST_STATUS_NONE)
            {
                handler.SendErrorMessage(LANG_COMMAND_QUEST_NOTFOUND, quest->GetQuestId());
                return;
            }

            // Add quest items for quests that require items
            for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
            {
                uint32 id = quest->RequiredItemId[x];
                uint32 count = quest->RequiredItemCount[x];
                if (!id || !count)
                {
                    continue;
                }

                uint32 curItemCount = player->GetItemCount(id, true);

                ItemPosCountVec dest;
                uint8 msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, id, count - curItemCount);
                if (msg == EQUIP_ERR_OK)
                {
                    Item* item = player->StoreNewItem(dest, id, true);
                    player->SendNewItem(item, count - curItemCount, true, false);
                }
            }

            // All creature/GO slain/casted (not required, but otherwise it will display "Creature slain 0/10")
            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            {
                int32 creature = quest->RequiredNpcOrGo[i];
                uint32 creatureCount = quest->RequiredNpcOrGoCount[i];

                if (creature > 0)
                {
                    if (CreatureTemplate const* creatureInfo = sObjectMgr->GetCreatureTemplate(creature))
                    {
                        for (uint16 z = 0; z < creatureCount; ++z)
                        {
                            player->KilledMonster(creatureInfo, ObjectGuid::Empty);
                        }
                    }
                }
                else if (creature < 0)
                {
                    for (uint16 z = 0; z < creatureCount; ++z)
                    {
                        player->KillCreditGO(creature);
                    }
                }
            }

            // player kills
            if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_PLAYER_KILL))
            {
                if (uint32 reqPlayers = quest->GetPlayersSlain())
                {
                    player->KilledPlayerCreditForQuest(reqPlayers, quest);
                }
            }

            // If the quest requires reputation to complete
            if (uint32 repFaction = quest->GetRepObjectiveFaction())
            {
                uint32 repValue = quest->GetRepObjectiveValue();
                uint32 curRep = player->GetReputationMgr().GetReputation(repFaction);
                if (curRep < repValue)
                {
                    if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                    {
                        player->GetReputationMgr().SetReputation(factionEntry, static_cast<float>(repValue));
                    }
                }
            }

            // If the quest requires a SECOND reputation to complete
            if (uint32 repFaction = quest->GetRepObjectiveFaction2())
            {
                uint32 repValue2 = quest->GetRepObjectiveValue2();
                uint32 curRep = player->GetReputationMgr().GetReputation(repFaction);
                if (curRep < repValue2)
                {
                    if (FactionEntry const* factionEntry = sFactionStore.LookupEntry(repFaction))
                    {
                        player->GetReputationMgr().SetReputation(factionEntry, static_cast<float>(repValue2));
                    }
                }
            }

            // If the quest requires money
            int32 ReqOrRewMoney = quest->GetRewOrReqMoney(player->GetLevel());
            if (ReqOrRewMoney < 0)
            {
                player->ModifyMoney(-ReqOrRewMoney);
            }

            player->CompleteQuest(quest->GetQuestId());
        }
        else
        {
            ObjectGuid::LowType guid = player->GetGUID().GetCounter();
            QueryResult result = CharacterDatabase.Query("SELECT 1 FROM character_queststatus WHERE guid = {} AND quest = {}", guid, quest->GetQuestId());

            if (!result)
            {
                handler.SendErrorMessage(LANG_COMMAND_QUEST_NOT_FOUND_IN_LOG, quest->GetTitle(), quest->GetQuestId());
                return;
            }

            CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

            typedef std::pair<uint32, uint32> items;
            std::vector<items> questItems;

            for (uint8 x = 0; x < QUEST_ITEM_OBJECTIVES_COUNT; ++x)
            {
                uint32 id = quest->RequiredItemId[x];
                uint32 count = quest->RequiredItemCount[x];
                if (!id || !count)
                {
                    continue;
                }

                questItems.push_back(std::make_pair(id, count));
            }

            if (!questItems.empty())
            {
                MailSender sender(MAIL_NORMAL, guid, MAIL_STATIONERY_GM);
                // fill mail
                MailDraft draft(quest->GetTitle(), std::string());

                for (auto const& itr : questItems)
                {
                    if (Item* item = Item::CreateItem(itr.first, itr.second))
                    {
                        item->SaveToDB(trans);
                        draft.AddItem(item);
                    }
                }

                draft.SendMailTo(trans, MailReceiver(nullptr, guid), sender);
            }

            uint8 index = 0;

            CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CHAR_QUESTSTATUS);
            stmt->SetData(index++, guid);
            stmt->SetData(index++, quest->GetQuestId());
            stmt->SetData(index++, 1);
            stmt->SetData(index++, quest->HasFlag(QUEST_FLAGS_EXPLORATION));
            stmt->SetData(index++, 0);

            for (uint8 i = 0; i < QUEST_OBJECTIVES_COUNT; i++)
            {
                stmt->SetData(index++, quest->RequiredNpcOrGoCount[i]);
            }

            for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; i++)
            {
                // Will be updated once they loot the items from the mailbox.
                stmt->SetData(index++, 0);
            }

            stmt->SetData(index, 0);

            trans->Append(stmt);

            // If the quest requires reputation to complete, set the player rep to the required amount.
            if (uint32 repFaction = quest->GetRepObjectiveFaction())
            {
                uint32 repValue = quest->GetRepObjectiveValue();

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_REP_BY_FACTION);
                stmt->SetData(0, repFaction);
                stmt->SetData(1, guid);
                PreparedQueryResult result = CharacterDatabase.Query(stmt);

                if (result)
                {
                    Field* fields = result->Fetch();
                    uint32 curRep = fields[0].Get<uint32>();

                    if (curRep < repValue)
                    {
                        if (sFactionStore.LookupEntry(repFaction))
                        {
                            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_REP_FACTION_CHANGE);
                            stmt->SetData(0, repFaction);
                            stmt->SetData(1, repValue);
                            stmt->SetData(2, repFaction);
                            stmt->SetData(3, guid);
                            trans->Append(stmt);
                        }
                    }
                }
            }

            // If the quest requires reputation to complete, set the player rep to the required amount.
            if (uint32 repFaction = quest->GetRepObjectiveFaction2())
            {
                uint32 repValue = quest->GetRepObjectiveValue();

                stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_REP_BY_FACTION);
                stmt->SetData(0, repFaction);
                stmt->SetData(1, guid);
                PreparedQueryResult result = CharacterDatabase.Query(stmt);

                if (result)
                {
                    Field* fields = result->Fetch();
                    uint32 curRep = fields[0].Get<uint32>();

                    if (curRep < repValue)
                    {
                        if (sFactionStore.LookupEntry(repFaction))
                        {
                            stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_REP_FACTION_CHANGE);
                            stmt->SetData(0, repFaction);
                            stmt->SetData(1, repValue);
                            stmt->SetData(2, repFaction);
                            stmt->SetData(3, guid);
                            trans->Append(stmt);
                        }
                    }
                }
            }

            CharacterDatabase.CommitTransaction(trans);
        }

        // check if Quest Tracker is enabled
        if (sWorld->getBoolConfig(CONFIG_QUEST_ENABLE_QUEST_TRACKER))
        {
            // prepare Quest Tracker datas
            auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_QUEST_TRACK_GM_COMPLETE);
            stmt->SetData(0, quest->GetQuestId());
            stmt->SetData(1, player->GetGUID().GetCounter());

            // add to Quest Tracker
            CharacterDatabase.Execute(stmt);
        }

        handler.PSendSysMessage(LANG_COMMAND_QUEST_COMPLETE, quest->GetTitle().c_str(), quest->GetQuestId());
        handler.SetSentErrorMessage(false);
    }

    // 其他逻辑
}


void WorldSession::HandleQuestQueryOpcode(WorldPacket& recvData)
{
    if (!_player)
        return;

    uint32 questId;
    recvData >> questId;
    LOG_DEBUG("network", "WORLD: Received CMSG_QUEST_QUERY quest = {}", questId);

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        _player->PlayerTalkClass->SendQuestQueryResponse(quest);
}

void WorldSession::HandleQuestgiverChooseRewardOpcode(WorldPacket& recvData)
{
    uint32 questId, reward;
    ObjectGuid guid;
    recvData >> guid >> questId >> reward;

    if (reward >= QUEST_REWARD_CHOICES_COUNT)
    {
        LOG_ERROR("network.opcode", "Error in CMSG_QUESTGIVER_CHOOSE_REWARD: player {} ({}) tried to get invalid reward ({}) (probably packet hacking)",
            _player->GetName(), _player->GetGUID().ToString(), reward);
        return;
    }

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_CHOOSE_REWARD npc {}, quest = {}, reward = {}", guid.ToString(), questId, reward);

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    // some kind of WPE protection
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if ((!_player->CanSeeStartQuest(quest) &&  _player->GetQuestStatus(questId) == QUEST_STATUS_NONE) ||
                (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE && !quest->IsAutoComplete() && quest->GetQuestMethod()))
        {
            LOG_ERROR("network.opcode", "HACK ALERT: Player {} ({}) is trying to complete quest (id: {}) but he has no right to do it!",
                           _player->GetName(), _player->GetGUID().ToString(), questId);
            return;
        }
        if (_player->CanRewardQuest(quest, reward, true))
        {
            _player->RewardQuest(quest, reward, object);

            // Special dialog status update (client does not query this)
            if (!quest->GetQuestMethod())
            {
                _player->PlayerTalkClass->SendQuestGiverStatus(uint8(_player->GetQuestDialogStatus(object)), guid);
            }

            switch (object->GetTypeId())
            {
                case TYPEID_UNIT:
                    {
                        Creature* questgiver = object->ToCreature();
                        if (!sScriptMgr->OnQuestReward(_player, questgiver, quest, reward))
                        {
                            // Send next quest
                            if (Quest const* nextQuest = _player->GetNextQuest(guid, quest))
                            {
                                if (_player->CanTakeQuest(nextQuest, false))
                                {
                                    if (nextQuest->IsAutoAccept())
                                    {
                                        // QUEST_FLAGS_AUTO_ACCEPT was not used by Blizzard.
                                        if (_player->CanAddQuest(nextQuest, false))
                                        {
                                            _player->AddQuestAndCheckCompletion(nextQuest, object);
                                        }
                                        else
                                        {
                                            // Auto accept is set for a custom quest and there is no inventory space
                                            _player->PlayerTalkClass->SendCloseGossip();
                                            break;
                                        }
                                    }
                                    _player->PlayerTalkClass->SendQuestGiverQuestDetails(nextQuest, guid, true);
                                }
                            }

                            questgiver->AI()->sQuestReward(_player, quest, reward);
                        }
                        break;
                    }
                case TYPEID_GAMEOBJECT:
                    {
                        GameObject* questGiver = object->ToGameObject();
                        if (!sScriptMgr->OnQuestReward(_player, questGiver, quest, reward))
                        {
                            // Send next quest
                            if (Quest const* nextQuest = _player->GetNextQuest(guid, quest))
                            {
                                if (_player->CanAddQuest(nextQuest, false) && _player->CanTakeQuest(nextQuest, false))
                                {
                                    if (nextQuest->IsAutoAccept())
                                        _player->AddQuestAndCheckCompletion(nextQuest, object);
                                    _player->PlayerTalkClass->SendQuestGiverQuestDetails(nextQuest, guid, true);
                                }
                            }

                            questGiver->AI()->QuestReward(_player, quest, reward);
                        }
                        break;
                    }
                default:
                    break;
            }
        }
        else
            _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
    }
}

void WorldSession::HandleQuestgiverRequestRewardOpcode(WorldPacket& recvData)
{
    uint32 questId;
    ObjectGuid guid;
    recvData >> guid >> questId;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_REQUEST_REWARD npc {}, quest = {}", guid.ToString(), questId);

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    // some kind of WPE protection
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    if (_player->CanCompleteQuest(questId))
        _player->CompleteQuest(questId);

    if (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
        return;

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
        _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
}

void WorldSession::HandleQuestgiverCancel(WorldPacket& /*recvData*/)
{
    _player->PlayerTalkClass->SendCloseGossip();
}

void WorldSession::HandleQuestLogSwapQuest(WorldPacket& recvData)
{
    uint8 slot1, slot2;
    recvData >> slot1 >> slot2;

    if (slot1 == slot2 || slot1 >= MAX_QUEST_LOG_SIZE || slot2 >= MAX_QUEST_LOG_SIZE)
        return;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTLOG_SWAP_QUEST slot 1 = {}, slot 2 = {}", slot1, slot2);

    GetPlayer()->SwapQuestSlot(slot1, slot2);
}

void WorldSession::HandleQuestLogRemoveQuest(WorldPacket& recvData)
{
    uint8 slot;
    recvData >> slot;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTLOG_REMOVE_QUEST slot = {}", slot);

    if (slot < MAX_QUEST_LOG_SIZE)
    {
        if (uint32 questId = _player->GetQuestSlotQuestId(slot))
        {
            if (!_player->TakeQuestSourceItem(questId, true))
                return;                                     // can't un-equip some items, reject quest cancel

            if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
            {
                if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_TIMED))
                    _player->RemoveTimedQuest(questId);

                if (quest->HasFlag(QUEST_FLAGS_FLAGS_PVP))
                {
                    _player->pvpInfo.IsHostile = _player->pvpInfo.IsInHostileArea || _player->HasPvPForcingQuest();
                    _player->UpdatePvPState();
                }
            }

            _player->TakeQuestSourceItem(questId, true); // remove quest src item from player
            _player->AbandonQuest(questId); // remove all quest items player received before abandoning quest.
            _player->RemoveActiveQuest(questId);
            _player->RemoveTimedAchievement(ACHIEVEMENT_TIMED_TYPE_QUEST, questId);

            sScriptMgr->OnQuestAbandon(_player, questId);

            LOG_DEBUG("network.opcode", "Player {} abandoned quest {}", _player->GetGUID().ToString(), questId);
            // check if Quest Tracker is enabled
            if (sWorld->getBoolConfig(CONFIG_QUEST_ENABLE_QUEST_TRACKER))
            {
                // prepare Quest Tracker datas
                auto stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_QUEST_TRACK_ABANDON_TIME);
                stmt->SetData(0, questId);
                stmt->SetData(1, _player->GetGUID().GetCounter());

                // add to Quest Tracker
                CharacterDatabase.Execute(stmt);
            }
        }

        _player->SetQuestSlot(slot, 0);

        _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_QUEST_ABANDONED, 1);
    }
}

void WorldSession::HandleQuestConfirmAccept(WorldPacket& recvData)
{
    uint32 questId;
    recvData >> questId;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUEST_CONFIRM_ACCEPT quest = {}", questId);

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (!quest->HasFlag(QUEST_FLAGS_PARTY_ACCEPT))
            return;

        Player* originalPlayer = ObjectAccessor::GetPlayer(*_player, _player->GetDivider());
        if (!originalPlayer)
            return;

        if (!_player->IsInSameRaidWith(originalPlayer) || !_player->IsAtGroupRewardDistance(originalPlayer))
            return;

        if (!_player->CanTakeQuest(quest, true) || _player->HasPendingBind())
            return;

        // pussywizard: exploit fix, can't share quests that give items to be sold
        if (uint32 itemId = quest->GetSrcItemId())
            if (ItemTemplate const* srcItem = sObjectMgr->GetItemTemplate(itemId))
                if (srcItem->SellPrice > 0)
                    return;

        if (_player->CanAddQuest(quest, true))
            _player->AddQuestAndCheckCompletion(quest, nullptr); // nullptr, this prevent DB script from duplicate running

        _player->SetDivider();
    }
}

void WorldSession::HandleQuestgiverCompleteQuest(WorldPacket& recvData)
{
    uint32 questId;
    ObjectGuid guid;

    recvData >> guid >> questId;

    LOG_DEBUG("network", "WORLD: Received CMSG_QUESTGIVER_COMPLETE_QUEST npc {}, quest = {}", guid.ToString(), questId);

    Object* object = ObjectAccessor::GetObjectByTypeMask(*_player, guid, TYPEMASK_UNIT | TYPEMASK_GAMEOBJECT);
    if (!object || !object->hasInvolvedQuest(questId))
        return;

    // some kind of WPE protection
    if (!_player->CanInteractWithQuestGiver(object))
        return;

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (!_player->CanSeeStartQuest(quest) && _player->GetQuestStatus(questId) == QUEST_STATUS_NONE)
        {
            LOG_ERROR("network.opcode", "Possible hacking attempt: Player {} [{}] tried to complete quest [entry: {}] without being in possession of the quest!",
                           _player->GetName(), _player->GetGUID().ToString(), questId);
            return;
        }

        if (Battleground* bg = _player->GetBattleground())
            if (bg->GetBgTypeID(true) == BATTLEGROUND_AV)
                bg->ToBattlegroundAV()->HandleQuestComplete(questId, _player);

        if (_player->GetQuestStatus(questId) != QUEST_STATUS_COMPLETE)
        {
            if (quest->IsRepeatable())
                _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanCompleteRepeatableQuest(quest), false);
            else
                _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanRewardQuest(quest, false), false);
        }
        else
        {
            if (quest->GetReqItemsCount())                  // some items required
                _player->PlayerTalkClass->SendQuestGiverRequestItems(quest, guid, _player->CanRewardQuest(quest, false), false);
            else                                            // no items required
                _player->PlayerTalkClass->SendQuestGiverOfferReward(quest, guid, true);
        }
    }
}

void WorldSession::HandleQuestgiverQuestAutoLaunch(WorldPacket& /*recvPacket*/)
{
}

void WorldSession::HandlePushQuestToParty(WorldPacket& recvPacket)
{
    uint32 questId;
    recvPacket >> questId;

    if (!_player->CanShareQuest(questId))
        return;

    LOG_DEBUG("network", "WORLD: Received CMSG_PUSHQUESTTOPARTY quest = {}", questId);

    if (Quest const* quest = sObjectMgr->GetQuestTemplate(questId))
    {
        if (Group* group = _player->GetGroup())
        {
            for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* player = itr->GetSource();

                if (!player || player == _player || !player->IsInMap(_player))         // skip self
                    continue;

                if (!player->SatisfyQuestStatus(quest, false))
                {
                    _player->SendPushToPartyResponse(player, QUEST_PARTY_MSG_HAVE_QUEST);
                    continue;
                }

                if (player->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
                {
                    _player->SendPushToPartyResponse(player, QUEST_PARTY_MSG_FINISH_QUEST);
                    continue;
                }

                if (!player->CanTakeQuest(quest, false))
                {
                    _player->SendPushToPartyResponse(player, QUEST_PARTY_MSG_CANT_TAKE_QUEST);
                    continue;
                }

                if (!player->SatisfyQuestLog(false))
                {
                    _player->SendPushToPartyResponse(player, QUEST_PARTY_MSG_LOG_FULL);
                    continue;
                }

                // Check if Quest Share in BG is enabled
                if (sWorld->getBoolConfig(CONFIG_BATTLEGROUND_DISABLE_QUEST_SHARE_IN_BG))
                {
                    // Check if player is in BG
                    if (_player->InBattleground())
                    {
                        _player->GetSession()->SendNotification(LANG_BG_SHARE_QUEST_ERROR);
                        continue;
                    }
                }

                if (player->GetDivider())
                {
                    _player->SendPushToPartyResponse(player, QUEST_PARTY_MSG_BUSY);
                    continue;
                }

                _player->SendPushToPartyResponse(player, QUEST_PARTY_MSG_SHARING_QUEST);

                if (quest->IsAutoAccept() && player->CanAddQuest(quest, true) && player->CanTakeQuest(quest, true))
                    player->AddQuestAndCheckCompletion(quest, _player);

                if (quest->IsAutoComplete() || !quest->GetQuestMethod())
                    player->PlayerTalkClass->SendQuestGiverRequestItems(quest, _player->GetGUID(), player->CanCompleteRepeatableQuest(quest), true);
                else
                {
                    player->SetDivider(_player->GetGUID());
                    player->PlayerTalkClass->SendQuestGiverQuestDetails(quest, player->GetGUID(), true);
                }
            }
        }
    }
}

void WorldSession::HandleQuestPushResult(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    uint32 questId;
    uint8 msg;
    recvPacket >> guid >> questId >> msg;

    if (_player->GetDivider() && _player->GetDivider() == guid)
    {
        if (Player* player = ObjectAccessor::GetPlayer(*_player, _player->GetDivider()))
        {
            WorldPacket data(MSG_QUEST_PUSH_RESULT, 8 + 4 + 1);
            data << _player->GetGUID();
            data << uint8(msg);                             // valid values: 0-8
            player->GetSession()->SendPacket(&data);
            _player->SetDivider();
        }
    }
}

void WorldSession::HandleQuestgiverStatusMultipleQuery(WorldPacket& /*recvPacket*/)
{
    _player->SendQuestGiverStatusMultiple();
}

void WorldSession::HandleQueryQuestsCompleted(WorldPacket& /*recvData*/)
{
    size_t rew_count = _player->GetRewardedQuestCount();

    WorldPacket data(SMSG_QUERY_QUESTS_COMPLETED_RESPONSE, 4 + 4 * rew_count);
    data << uint32(rew_count);

    const RewardedQuestSet& rewQuests = _player->getRewardedQuests();
    for (RewardedQuestSet::const_iterator itr = rewQuests.begin(); itr != rewQuests.end(); ++itr)
        data << uint32(*itr);

    SendPacket(&data);
}
