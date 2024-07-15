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


enum WarriorSpells
{
    SPELL_AURA_1          = 102061, // 增强光环1，假设伤害提高10%，减速30%
    SPELL_AURA_2          = 102063, // 增强光环2，假设伤害提高20%，减速40%
    SPELL_AURA_3          = 102065, // 增强光环3，假设伤害提高30%，减速50%
    SLOW_EFFECT_1         = 102062, // 减速效果1
    SLOW_EFFECT_2         = 102064, // 减速效果2
    SLOW_EFFECT_3         = 102066, // 减速效果3

};

class spell_custom_consecration : public SpellScriptLoader
{
public:
    spell_custom_consecration() : SpellScriptLoader("spell_custom_consecration") { }

    class spell_custom_consecration_AuraScript : public AuraScript
    {
        PrepareAuraScript(spell_custom_consecration_AuraScript);

        void CalculateAmount(AuraEffect const* /* aurEff */, int32& amount, bool& /* canBeRecalculated */)
        {
            if (Unit* caster = GetCaster())
            {
                // 检查不同的光环并调整伤害
                if (caster->HasAura(SPELL_AURA_1))
                {
                    amount = CalculatePct(amount, 110); // 提高伤害10%
                    LOG_INFO("spell", "玩家 {} 使用了奉献，伤害提高了10%。", caster->GetName().c_str());
                }
                else if (caster->HasAura(SPELL_AURA_2))
                {
                    amount = CalculatePct(amount, 120); // 提高伤害20%
                    LOG_INFO("spell", "玩家 {} 使用了奉献，伤害提高了20%。", caster->GetName().c_str());
                }
                else if (caster->HasAura(SPELL_AURA_3))
                {
                    amount = CalculatePct(amount, 130); // 提高伤害30%
                    LOG_INFO("spell", "玩家 {} 使用了奉献，伤害提高了30%。", caster->GetName().c_str());
                }
            }
        }

        void HandleEffectApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
        {
            if (Unit* target = GetTarget())
            {
                if (Unit* caster = GetCaster())
                {
                    // 检查不同的光环并施加相应的减速效果
                    if (caster->HasAura(SPELL_AURA_1))
                    {
                        target->CastSpell(target, SLOW_EFFECT_1, true); // 减速30%
                        LOG_INFO("spell", "玩家 {} 使用了奉献，使范围内的目标减速30%。", caster->GetName().c_str());
                    }
                    else if (caster->HasAura(SPELL_AURA_2))
                    {
                        target->CastSpell(target, SLOW_EFFECT_2, true); // 减速40%
                        LOG_INFO("spell", "玩家 {} 使用了奉献，使范围内的目标减速40%。", caster->GetName().c_str());
                    }
                    else if (caster->HasAura(SPELL_AURA_3))
                    {
                        target->CastSpell(target, SLOW_EFFECT_3, true); // 减速50%
                        LOG_INFO("spell", "玩家 {} 使用了奉献，使范围内的目标减速50%。", caster->GetName().c_str());
                    }
                }
            }
        }

        void Register() override
        {
            DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_custom_consecration_AuraScript::CalculateAmount, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE);
            OnEffectApply += AuraEffectApplyFn(spell_custom_consecration_AuraScript::HandleEffectApply, EFFECT_0, SPELL_AURA_PERIODIC_DAMAGE, AURA_EFFECT_HANDLE_REAL);
        }
    };

    AuraScript* GetAuraScript() const override
    {
        return new spell_custom_consecration_AuraScript();
    }
};



//修改自律技能持续时间20秒.
#define SPELL_ENHANCED_AUTONOMY_AURA 102101

class spell_paladin_autonomy : public AuraScript
{
    PrepareAuraScript(spell_paladin_autonomy);

    void OnApply(AuraEffect const* aurEff, AuraEffectHandleModes /*mode*/)
    {
        Unit* caster = GetCaster();
        Aura* aura = GetAura();

        // 检查施法者是否有强化自律光环
        if (caster && caster->HasAura(SPELL_ENHANCED_AUTONOMY_AURA))
        {
            // 修改“自律”技能的持续时间为20秒
            aura->SetDuration(10000); // 持续时间20秒
            aura->SetMaxDuration(10000);
            LOG_INFO("spell", "玩家 {} 拥有技能光环 {}，自律技能持续时间修改为20秒。", caster->GetName().c_str(), SPELL_ENHANCED_AUTONOMY_AURA);
        }
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_paladin_autonomy::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};


void AddSC_custom_paladin_spell_scripts()
{
    new spell_custom_consecration();
    new spell_paladin_autonomy();
}




 
