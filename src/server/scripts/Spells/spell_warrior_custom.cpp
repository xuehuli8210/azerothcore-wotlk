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


#include "CreatureScript.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "Chat.h"
#include "MotionMaster.h"
#include "SpellAuraEffects.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "SpellScript.h"
#include "SpellScriptLoader.h"
#include "GameTime.h"
#include "MovementGenerator.h"
#include "PointMovementGenerator.h" 




//INSERT INTO `acore_tworld`.`spell_script_names`(`spell_id`, `ScriptName`) VALUES (47436, 'spell_warrior_shouts');
//INSERT INTO `acore_tworld`.`spell_script_names`(`spell_id`, `ScriptName`) VALUES (47440, 'spell_warrior_shouts');
//INSERT INTO `acore_tworld`.`spell_script_names`(`spell_id`, `ScriptName`) VALUES (47437, 'spell_warrior_shouts');
enum WarriorSpells
{
    SPELL_WARRIOR_SPECIAL_AURA          = 101041, // 强化怒吼
    SPELL_STUN_EFFECT_AURA              = 101051, // 强化英勇投掷后昏迷5秒光环
    SPELL_STUN_EFFECT                   = 101052, // 强化英勇投掷后昏迷5秒
};

//47436 战斗怒吼
class spell_warrior_battle_shout : public AuraScript
{
    PrepareAuraScript(spell_warrior_battle_shout);
    
    void CalculateAmount(AuraEffect const* /* aurEff */, int32& amount, bool& /*canBeRecalculated*/)
    {
        if (Unit* target = GetUnitOwner())
        {
            // 检查是否存在强化怒吼
            if (!target->HasAura(SPELL_WARRIOR_SPECIAL_AURA))
            {
                return;
            }

            int32 attackPowerBonus = CalculatePct(target->GetTotalAttackPowerValue(BASE_ATTACK), 10);
            amount = attackPowerBonus;
            LOG_INFO("spell", "玩家 {} 使用了战斗怒吼，增加了 {} 的攻击强度。", target->GetName().c_str(), amount);
        }
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_warrior_battle_shout::CalculateAmount, EFFECT_0, SPELL_AURA_MOD_ATTACK_POWER);
         DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_warrior_battle_shout::CalculateAmount, EFFECT_1, SPELL_AURA_MOD_RANGED_ATTACK_POWER);
    }
};
//47440 命令怒吼
class spell_warrior_commanding_shout : public AuraScript
{
    PrepareAuraScript(spell_warrior_commanding_shout);

    void CalculateAmount(AuraEffect const* /* aurEff */, int32& amount, bool& /*canBeRecalculated*/)
    {
        if (Unit* target = GetUnitOwner())
        {
            // 检查是否存在强化怒吼
           if (!target->HasAura(SPELL_WARRIOR_SPECIAL_AURA))
            {
                return;
            }

            int32 healthBonus = CalculatePct(target->GetMaxHealth(), 10);
            amount = healthBonus;
            LOG_INFO("spell", "玩家 {} 使用了命令怒吼，增加了 {} 的最大生命值。", target->GetName().c_str(), amount);
        }
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_warrior_commanding_shout::CalculateAmount, EFFECT_0, SPELL_AURA_230);
    }
};

//47437 挫志怒吼
class spell_warrior_demoralizing_shout : public AuraScript
{
    PrepareAuraScript(spell_warrior_demoralizing_shout);

    void CalculateAmount(AuraEffect const* /* aurEff */, int32& amount, bool& /*canBeRecalculated*/)
    {
        if (Unit* target = GetUnitOwner())
        {
            // 检查是否存在强化怒吼
            if (!target->HasAura(SPELL_WARRIOR_SPECIAL_AURA))
            {
                return;
            }

            int32 attackPowerReduction = CalculatePct(target->GetTotalAttackPowerValue(BASE_ATTACK), 8);
            amount = -attackPowerReduction;
            LOG_INFO("spell", "玩家 {} 使用了挫志怒吼，降低了 {} 的攻击强度。", target->GetName().c_str(), amount);
        }
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_warrior_demoralizing_shout::CalculateAmount, EFFECT_0, SPELL_AURA_MOD_DAMAGE_DONE);
    }
};

//57755 英勇投掷
class spell_warr_heroic_throw_stun : public SpellScript
{
    PrepareSpellScript(spell_warr_heroic_throw_stun);
    bool Load() override
    {
        return GetCaster()->GetTypeId() == TYPEID_PLAYER;
    }
    void HandleAfterHit()
    {
        Player* player = GetCaster()->ToPlayer();
        if (GetCaster()->ToPlayer()->HasAura(SPELL_STUN_EFFECT_AURA))
        {
           if (Unit* target = GetHitUnit())
            {
                GetCaster()->ToPlayer()->CastSpell(target, SPELL_STUN_EFFECT, true);
            }
        }
    }

    void Register() override
    {
        AfterHit += SpellHitFn(spell_warr_heroic_throw_stun::HandleAfterHit);
    }
};




// 注册脚本
void AddSC_custom_warrior_spell_scripts()
{
    RegisterSpellScript(spell_warrior_battle_shout); //47436 战斗怒吼
    RegisterSpellScript(spell_warrior_commanding_shout); //47440 命令怒吼
    RegisterSpellScript(spell_warrior_demoralizing_shout); //47437 挫志怒吼

    RegisterSpellScript(spell_warr_heroic_throw_stun); //英勇投掷昏迷5秒

}



 
