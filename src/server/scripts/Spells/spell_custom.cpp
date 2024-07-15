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

// 发送冷却数据包的函数
void SendCooldownPacket(Player* player, uint32 spellId, uint32 cooldown) {
    WorldPacket data(SMSG_SPELL_COOLDOWN, 8 + 1 + 4 + 4);
    data << player->GetGUID();   // 玩家的GUID
    data << uint8(0);            // 标志，通常用于特殊情况
    data << uint32(spellId);     // 法术ID
    data << uint32(cooldown);    // 冷却时间
    player->GetSession()->SendPacket(&data);
}

//修改护盾使他可以生成25%的生命值
class spell_custom_health_gurad : public AuraScript
{
    PrepareAuraScript(spell_custom_health_gurad);

    void CalculateAmount(AuraEffect const* /*aurEff*/, int32& amount, bool& /*canBeRecalculated*/)
    {
        if (Unit* caster = GetCaster())
        {
            int32 healthModSpellBasePoints0 = int32(caster->CountPctFromMaxHealth(25));
            amount = healthModSpellBasePoints0;
        }
    }

    void Register() override
    {
        DoEffectCalcAmount += AuraEffectCalcAmountFn(spell_custom_health_gurad::CalculateAmount, EFFECT_0, SPELL_AURA_SCHOOL_ABSORB);
    }
};


//光环结束处理神圣马驹CD
class aura_80020_expire_script : public AuraScript
{
    PrepareAuraScript(aura_80020_expire_script);

    void OnRemove(AuraEffect const* /* aurEff */, AuraEffectHandleModes /* mode*/)
    {
        if (Unit* target = GetTarget()) {
            uint32 cooldown = 120 * IN_MILLISECONDS; // 120秒冷却
            target->AddSpellCooldown(80021, 0, cooldown );
            
            Player* player = target->ToPlayer(); // 确保转换为 Player 类型
            if (player) {
                SendCooldownPacket(player, 80021, cooldown);
            }
        } 
    }


    void Register() override
    {
        OnEffectRemove += AuraEffectRemoveFn(aura_80020_expire_script::OnRemove, EFFECT_0, SPELL_AURA_MOD_SPEED_ALWAYS, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }
};




//根据光环 处理神圣马驹
class spell_divine_steed_charges : public SpellScript
{
    PrepareSpellScript(spell_divine_steed_charges);

    void HandleOnCast()
    {
        if (GetCaster())
        {
            Player* player = GetCaster()->ToPlayer(); // 确保转换为 Player 类型
            if (player)
            {
                if (!player->HasAura(80020))
                {
                    player->RemoveSpellCooldown(GetSpellInfo()->Id,true); // Apply 35-second 
                    player->AddAura(80020, player); // 给玩家添加光环
                    if (Aura* aura = player->GetAura(80020)) // 获取这个新施放的光环
                    {
                        aura->SetDuration(10 * IN_MILLISECONDS); // 设置持续时间为10000毫秒（10秒）
                    }
                }else{
                    player->RemoveAura(80020); // 移除ID为80020的光环
                }
            }
        }
    }

    void Register() override
    {
        AfterCast += SpellCastFn(spell_divine_steed_charges::HandleOnCast);
    }
};

// 操控时间
class spell_mage_alter_time_aura : public AuraScript
{
    PrepareAuraScript(spell_mage_alter_time_aura);

    void OnApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        Player* player = target->ToPlayer(); // 确保转换为 Player 类型
        if (player) {
          
            if (GetAura()->GetStackAmount() == 1) {
                // 第一次应用时保存玩家的状态
                _health = player->GetHealth();
                _position = player->GetPosition();
                _mapId = player->GetMapId(); // 保存当前地图ID
                _orientation = player->GetOrientation(); // 保存玩家的朝向
                player->RemoveSpellCooldown(GetSpellInfo()->Id, true);

                 
                // 使用 SummonGameObject 生成发光 GameObject
                GameObject* glowObject = player->SummonGameObject(193794, _position.GetPositionX(), _position.GetPositionY(), _position.GetPositionZ(), _orientation, 0, 0, 0, 0, 10);


            } else if (GetAura()->GetStackAmount() == 2) {
                // 第二次应用时检查是否需要执行与移除相同的操作
                player->RemoveAura(GetSpellInfo()->Id); 
                //ExecuteRemoveEffect(player);
            }
        }
    }

    void OnRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
    {
        Unit* target = GetTarget();
        Player* player = target->ToPlayer(); // 确保转换为 Player 类型
        if (player) {
           
            uint32 cooldown = 90 * IN_MILLISECONDS; // 90秒冷却
            SendCooldownPacket(player, GetSpellInfo()->Id, cooldown);
            ExecuteRemoveEffect(player);
        }
    }

    void ExecuteRemoveEffect(Player* player) {
        float distance = player->GetDistance(_position);
      
        
        if (distance <= 60.0f && player->IsAlive()) {
            if (player->GetMaxHealth() > _health)
                player->SetHealth(_health);
            else
                player->SetFullHealth();
                
            player->CastSpell(player, 12980); //释放传送

            // 使用 TeleportTo 替换 Relocate
            player->TeleportTo(_mapId, _position.GetPositionX(), _position.GetPositionY(), _position.GetPositionZ(), _orientation);
           
        }
    }

    void Register() override
    {
        OnEffectApply += AuraEffectApplyFn(spell_mage_alter_time_aura::OnApply, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
        OnEffectRemove += AuraEffectRemoveFn(spell_mage_alter_time_aura::OnRemove, EFFECT_0, SPELL_AURA_DUMMY, AURA_EFFECT_HANDLE_REAL_OR_REAPPLY_MASK);
    }

private:
    uint32 _health = 0;
    Position _position;
    uint32 _mapId; // 用于存储地图ID
    float _orientation; // 用于存储朝向
};



class spell_custom_double_jump : public SpellScriptLoader
{
public:
    spell_custom_double_jump() : SpellScriptLoader("spell_custom_double_jump") { }

    class spell_custom_double_jump_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_custom_double_jump_SpellScript);

        void HandleAfterCast()
        {
            if (Player* player = GetCaster()->ToPlayer())
            {
                if (!player->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR))
                {
                    // 获取玩家当前的位置
                    float x, y, z;
                    player->GetPosition(x, y, z);

                    // 获取玩家当前的朝向
                    float orientation = player->GetOrientation();

                    // 计算新的目标位置
                    float distance = 15.0f; // 前方跳跃距离
                    float jumpHeight = 15.0f; // 跳跃高度

                    // 计算新位置的X和Y坐标，这取决于玩家的朝向
                    float newX = x + distance * std::cos(orientation);
                    float newY = y + distance * std::sin(orientation);
                    float newZ = z + jumpHeight; // 新的Z坐标是当前的Z坐标加上跳跃高度

                    // 根据需要调整速度值
                    float speedXY = player->GetSpeed(MOVE_RUN) * 1.5; // 设定速度是跑动速度的1.5倍
                    float speedZ = jumpHeight * 2; // 垂直速度可能需要调整，以达到期望的高度

                    // 调用跳跃函数
                    player->GetMotionMaster()->MoveJump(newX, newY, newZ, speedXY, speedZ);
                }
            }
        }


        void Register() override
        {
            AfterCast += SpellCastFn(spell_custom_double_jump_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_double_jump_SpellScript();
    }
};

class spell_custom_heroic_leap : public SpellScriptLoader
{
public:
    spell_custom_heroic_leap() : SpellScriptLoader("spell_custom_heroic_leap") { }

    class spell_custom_heroic_leap_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_custom_heroic_leap_SpellScript);
        void HandleBeforeCast()
        {
             if (Player* player = GetCaster()->ToPlayer())
            {
                // 获取当前施法实例
                Spell* spell = GetSpell();
                if (spell)
                {
                    // 修改施法视觉效果ID
                    uint32 newSpellVisualId = 29; // 替换为你希望使用的视觉效果ID

                    // 使用反射来修改内部的 SpellVisual 数组
                    auto spellInfo = const_cast<SpellInfo*>(spell->GetSpellInfo());
                    spellInfo->SpellVisual[0] = newSpellVisualId;
                }
            }
        }

        
        void HandleAfterCast()
        {
            if (Player* player = GetCaster()->ToPlayer())
            {
                
                // 获取点击的坐标
                Position const& dest = GetExplTargetDest()->GetPosition();
               
                // 设置跳跃的速度参数
                float speedXY = 45.0f; // 水平方向速度
                float speedZ = 10.0f; // 垂直方向速度

                        // 执行双手武器跳起来劈砍动作
               
                // 使玩家跳跃到目标位置
                player->GetMotionMaster()->MoveJump(dest.GetPositionX(), dest.GetPositionY(), dest.GetPositionZ(), speedXY, speedZ);

                 // 执行双手武器跳起来劈砍动作
                //player->HandleEmoteCommand(EMOTE_ONESHOT_ATTACK2HTIGHT ); // 尽量接近双手武器攻击的动作

                // 注册玩家更新事件
                player->m_Events.AddEvent(new HeroicLeapEvent(player, 30), player->m_Events.CalculateTime(200)); // 添加最大尝试次数参数
                
            }
        }

        void Register() override
        {
           // BeforeCast += SpellCastFn(spell_custom_heroic_leap_SpellScript::HandleBeforeCast);
            AfterCast += SpellCastFn(spell_custom_heroic_leap_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_custom_heroic_leap_SpellScript();
    }

private:
    class HeroicLeapEvent : public BasicEvent
    {
    public:
        HeroicLeapEvent(Player* player, uint32 maxAttempts) : _player(player), _attempts(0), _maxAttempts(maxAttempts) { }

        bool Execute(uint64 /*eventTime*/, uint32 /*updateTime*/) override
        {
            if (!_player || !_player->IsInWorld())
                return true;
          
            MotionMaster* motionMaster = _player->GetMotionMaster();
            MovementGenerator* movementGenerator = motionMaster->GetMotionSlot(MOTION_SLOT_CONTROLLED);

            if (movementGenerator && movementGenerator->GetMovementGeneratorType() == EFFECT_MOTION_TYPE)
            {
            
                EffectMovementGenerator* effectMovement = static_cast<EffectMovementGenerator*>(movementGenerator);
                if (effectMovement && effectMovement->Update(_player, 0))
                {
                    _player->m_Events.AddEvent(new HeroicLeapEvent(_player, _maxAttempts), _player->m_Events.CalculateTime(10));
                    return false;
                }
                else
                {
                    OnJumpComplete();
                    return true;
                }
            } else
            {
                OnJumpComplete();
                return true;
            }

            return false;
        }

        void OnJumpComplete()
        {
            _player->CastSpell(_player, 64781, true); // 例如：施放一个着陆效果的法术
        }

    private:
        Player* _player;
        uint32 _attempts;
        uint32 _maxAttempts;
    };
};
 
class spell_paladin_trigger_avenging_wrath : public SpellScriptLoader
{
public:
    spell_paladin_trigger_avenging_wrath() : SpellScriptLoader("spell_paladin_trigger_avenging_wrath") { }

    class spell_paladin_trigger_avenging_wrath_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_paladin_trigger_avenging_wrath_SpellScript);

        void HandleAfterHit()
        {
            Unit* caster = GetCaster();

            if (!caster)
                return;
            if (Player* player = caster->ToPlayer())
            {
                // 如果玩家拥有光环25771则无法触发复仇之怒 没有80061 也return
                if (player->HasAura(25771) || !player->HasAura(80061))
                    return;

                // 10%的几率触发复仇之怒
                if (roll_chance_i(10))
                {
                    player->CastSpell(player, 31884, true); // 触发复仇之怒

                    // 设置复仇之怒持续时间为8秒
                    if (Aura* avengingWrath = player->GetAura(31884))
                        avengingWrath->SetDuration(10000);
                   
                    
                    player->RemoveSpellCooldown(31884, true);

                    // 清除光环61987和61988
                    player->RemoveAura(61987); 
                    player->RemoveAura(61988);
                }
             
            }
        }

        void Register() override
        {
            AfterHit += SpellHitFn(spell_paladin_trigger_avenging_wrath_SpellScript::HandleAfterHit);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_paladin_trigger_avenging_wrath_SpellScript();
    }
};

class spell_modify_cooldown : public SpellScriptLoader
{
public:
    spell_modify_cooldown() : SpellScriptLoader("spell_modify_cooldown") { }

    class spell_modify_cooldown_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_modify_cooldown_SpellScript);

        void HandleAfterCast()
        {
            Unit* caster = GetCaster();
            if (caster && caster->GetTypeId() == TYPEID_PLAYER)
            {
                Player* player = caster->ToPlayer();

                SpellInfo const* spellInfo = GetSpellInfo();

                struct EffectData
                {
                    uint32 triggerSpellId;
                    int32 basePoints;
                };

                EffectData effects[3] = {
                    { spellInfo->Effects[EFFECT_0].TriggerSpell, spellInfo->Effects[EFFECT_0].BasePoints },
                    { spellInfo->Effects[EFFECT_1].TriggerSpell, spellInfo->Effects[EFFECT_1].BasePoints },
                    { spellInfo->Effects[EFFECT_2].TriggerSpell, spellInfo->Effects[EFFECT_2].BasePoints }
                };

                for (const auto& effect : effects)
                {
                    if (effect.triggerSpellId != 0 && effect.basePoints != 0)
                    {
                        // 获取目标技能的SpellInfo
                        const SpellInfo* targetSpellInfo = sSpellMgr->GetSpellInfo(effect.triggerSpellId);
                        if (!targetSpellInfo)
                        {
                            continue;
                        }
                        // 获取目标技能的总冷却时间（以毫秒为单位）
                        uint32 totalCooldown = targetSpellInfo->GetRecoveryTime();
                        // 获取目标技能的当前冷却剩余时间（以毫秒为单位）
                        uint32 currentCooldownRemaining = player->GetSpellCooldownDelay(effect.triggerSpellId);
                        
                        if (currentCooldownRemaining > 0)
                        {
                            // 计算冷却时间调整值
                            int64 cooldownAdjustment = (static_cast<int64>(totalCooldown) * static_cast<int64>(effect.basePoints)) / 100;
                            
                            // 计算新的冷却剩余时间
                            int64 newCooldownRemaining = static_cast<int64>(currentCooldownRemaining) + cooldownAdjustment;
                            
                            // 确保冷却时间不为负值
                            newCooldownRemaining = std::max(newCooldownRemaining, static_cast<int64>(0));
                            // 重置目标技能的冷却时间
                            player->RemoveSpellCooldown(effect.triggerSpellId, true);
                            if(newCooldownRemaining > 0 ){
                                player->AddSpellCooldown(effect.triggerSpellId, 0, static_cast<uint32>(newCooldownRemaining));
                                SendCooldownPacket(player, effect.triggerSpellId, static_cast<uint32>(newCooldownRemaining));
                            }
                        }
                    }
                }
            }
        }

        void Register() override
        {
            AfterCast += SpellCastFn(spell_modify_cooldown_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_modify_cooldown_SpellScript();
    }
};


enum FeignRecoverySpellIds
{
    SPELL_FEIGN_RECOVERY = 80080,
    SPELL_FEINT = 80081
};

// 48659 佯攻
class spell_rogue_feint : public SpellScriptLoader
{
public:
    spell_rogue_feint() : SpellScriptLoader("spell_rogue_feint") { }

    class spell_rogue_feint_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_rogue_feint_SpellScript);

        void HandleBeforeCast()
        {
            LOG_INFO("spell", "HandleBeforeCast: 开始执行。");
            Unit* caster = GetCaster();
            if (caster)
            {
                LOG_INFO("spell", "HandleBeforeCast: 获取到施法者。");
            }
            else
            {
                LOG_INFO("spell", "HandleBeforeCast: 未获取到施法者。");
            }
        }

        void HandleAfterCast()
        {
            LOG_INFO("spell", "HandleAfterCast: 开始执行。");
            if (Unit* caster = GetCaster())
            {
                LOG_INFO("spell", "HandleAfterCast: 获取到施法者。");
                if (caster->GetTypeId() == TYPEID_PLAYER)
                {
                    LOG_INFO("spell", "HandleAfterCast: 施法者是玩家。");
                    Player* player = caster->ToPlayer();
                    if (player->HasAura(SPELL_FEINT))
                    {
                        LOG_INFO("spell", "HandleAfterCast: 玩家拥有佯攻光环。");
                        player->CastSpell(player, SPELL_FEIGN_RECOVERY, true);
                        LOG_INFO("spell", "玩家 {} 施放了佯攻，触发了假死恢复。", player->GetName().c_str());
                    }
                    else
                    {
                        LOG_INFO("spell", "玩家 {} 施放了佯攻，但未触发假死恢复。", player->GetName().c_str());
                    }
                }
                else
                {
                    LOG_INFO("spell", "HandleAfterCast: 施法者不是玩家。");
                }
            }
            else
            {
                LOG_INFO("spell", "HandleAfterCast: 未获取到施法者。");
            }
        }

        void HandleOnHit()
        {
            LOG_INFO("spell", "HandleOnHit: 开始执行。");
            Unit* caster = GetCaster();
            Unit* target = GetHitUnit();
            if (caster)
            {
                LOG_INFO("spell", "HandleOnHit: 获取到施法者。");
                if (target)
                {
                    LOG_INFO("spell", "HandleOnHit: 命中目标 {}。", target->GetName().c_str());
                }
                else
                {
                    LOG_INFO("spell", "HandleOnHit: 未获取到命中目标。");
                }
            }
            else
            {
                LOG_INFO("spell", "HandleOnHit: 未获取到施法者。");
            }
        }


       

        void Register() override
        {
            LOG_INFO("spell", "Register: 注册各种事件处理函数。");
            BeforeCast += SpellCastFn(spell_rogue_feint_SpellScript::HandleBeforeCast);
            AfterCast += SpellCastFn(spell_rogue_feint_SpellScript::HandleAfterCast);
            OnHit += SpellHitFn(spell_rogue_feint_SpellScript::HandleOnHit);
         
        }
    };

    SpellScript* GetSpellScript() const override
    {
        LOG_INFO("spell", "GetSpellScript: 创建 spell_rogue_feint_SpellScript 实例。");
        return new spell_rogue_feint_SpellScript();
    }
};
enum HemorrhageRecoverySpellIds
{
    SPELL_HEMORRHAGE_RECOVERY = 80090, // 新增的恢复能量的技能ID
    SPELL_HEMORRHAGE = 80091           // 出血技能的ID
};

class spell_rogue_hemorrhage : public SpellScriptLoader
{
public:
    spell_rogue_hemorrhage() : SpellScriptLoader("spell_rogue_hemorrhage") { }

    class spell_rogue_hemorrhage_SpellScript : public SpellScript
    {
        PrepareSpellScript(spell_rogue_hemorrhage_SpellScript);

        void HandleAfterCast()
        {
            Unit* caster = GetCaster();
            if (caster && caster->HasAura(SPELL_HEMORRHAGE))
            {
                caster->CastSpell(caster, SPELL_HEMORRHAGE_RECOVERY, true);
            }
        }

        void Register() override
        {
            AfterCast += SpellCastFn(spell_rogue_hemorrhage_SpellScript::HandleAfterCast);
        }
    };

    SpellScript* GetSpellScript() const override
    {
        return new spell_rogue_hemorrhage_SpellScript();
    }
};

// 在相应的数据库或脚本文件中添加SPELL_HEMORRHAGE_RECOVERY技能的定义，使其能够恢复40点能量


enum RogueAssassinationSpellIds
{
    SPELL_STEALTH = 1784, // 潜行技能的ID
    SPELL_ASSASSINATION = 80101, // 潜行刺杀光环技能ID
    SPELL_MASTER_ASSASSIN = 80100 // 刺客大师光环技能ID
};
//1784 潜行

class spell_rogue_stealth : public SpellScriptLoader
{
public:
    spell_rogue_stealth() : SpellScriptLoader("spell_rogue_stealth") { }

    class spell_rogue_stealth_AuraScript : public AuraScript
    {
        PrepareAuraScript(spell_rogue_stealth_AuraScript);

        void HandleApply(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
        {
            Unit* caster = GetCaster();
        
            if (caster && caster->HasAura(SPELL_ASSASSINATION))
            {
                LOG_INFO("spell_rogue_stealth", "HandleApply: 施法者有 SPELL_ASSASSINATION 光环");
                caster->CastSpell(caster, SPELL_MASTER_ASSASSIN, true);
                LOG_INFO("spell_rogue_stealth", "HandleApply: 施法者施放 SPELL_MASTER_ASSASSIN");
            }
        }

        void HandleRemove(AuraEffect const* /*aurEff*/, AuraEffectHandleModes /*mode*/)
        {
            if (Unit* caster = GetCaster())
            {
                if (caster->HasAura(SPELL_ASSASSINATION))
                {

                    Aura* aura = caster->GetAura(SPELL_MASTER_ASSASSIN);
                    if (aura)
                    {
                        LOG_INFO("spell_rogue_stealth", "HandleRemove: 找到 SPELL_MASTER_ASSASSIN 光环，修改持续时间为 5 秒");
                        aura->SetDuration(5000); // 修改持续时间为5秒（5000毫秒）
                    }
                }
            }
           
        }

        void Register() override
        {
            OnEffectApply += AuraEffectApplyFn(spell_rogue_stealth_AuraScript::HandleApply, EFFECT_0, SPELL_AURA_MOD_SHAPESHIFT, AURA_EFFECT_HANDLE_REAL);
            OnEffectRemove += AuraEffectRemoveFn(spell_rogue_stealth_AuraScript::HandleRemove, EFFECT_0, SPELL_AURA_MOD_SHAPESHIFT, AURA_EFFECT_HANDLE_REAL);
        }
    };

    AuraScript* GetAuraScript() const override
    {
        return new spell_rogue_stealth_AuraScript();
    }
};

class spell_mage_custom_damage : public SpellScript
{
    PrepareSpellScript(spell_mage_custom_damage);

    void HandleDamage(SpellEffIndex /*effIndex*/)
    {
        if (Unit* caster = GetCaster())
        {
            int32 damage = CalculatePct(caster->GetTotalAttackPowerValue(BASE_ATTACK), 40); // 40% of Attack Power
            SetHitDamage(damage);
        }
    }

    void Register() override
    {
        OnEffectHitTarget += SpellEffectFn(spell_mage_custom_damage::HandleDamage, EFFECT_0, SPELL_EFFECT_SCHOOL_DAMAGE);
    }
};
//48127


class spell_pri_mind_blast : public SpellScriptLoader
{
    public:
        spell_pri_mind_blast() : SpellScriptLoader("spell_pri_mind_blast") { }

        class spell_pri_mind_blast_SpellScript : public SpellScript
        {
            PrepareSpellScript(spell_pri_mind_blast_SpellScript);

            void FilterTargets(std::list<WorldObject*>& targets)
            {
                LOG_INFO("spells", "开始排序目标。");
                targets.sort(Acore::ObjectDistanceOrderPred(GetHitUnit()));

                if (Unit* caster = GetCaster())
                {
                    LOG_INFO("spells", "清除并重新选择目标。");
                    targets.clear();

                    std::list<Unit*> unitTargets;
                    Acore::AnyUnfriendlyUnitInObjectRangeCheck u_check(caster, caster, 10.0f);
                    Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> searcher(caster, unitTargets, u_check);
                    Cell::VisitAllObjects(caster, searcher, caster->GetVisibilityRange());

                    for (auto& unit : unitTargets)
                    {
                        targets.push_back(unit);
                    }
                }
            }

            void HandleAfterHit()
            {
                if (Unit* caster = GetCaster())
                {
                    if (Unit* target = GetHitUnit())
                    {
                        int32 damage = GetHitDamage();
                        int32 newdamage = CalculatePct(damage, 175);
                        LOG_INFO("spells", "给目标 老伤害值为 {}。 新伤害值为 {}。", damage,newdamage);
                        SetHitDamage(newdamage);
                      

                        int32 healAmount = CalculatePct(newdamage, 25);
                        caster->CastCustomSpell(caster, 18984, &healAmount, nullptr, nullptr, true);
                        LOG_INFO("spells", "给施法者 恢复生命值 {}。",  healAmount);
                    }
                }
            }

            void Register() override
            {
               // OnObjectTargetSelect += SpellObjectTargetSelectFn(spell_pri_mind_blast_SpellScript::FilterTargets, EFFECT_0, TARGET_UNIT_TARGET_ENEMY);
                AfterHit += SpellHitFn(spell_pri_mind_blast_SpellScript::HandleAfterHit);
            }
        };

        SpellScript* GetSpellScript() const override
        {
            return new spell_pri_mind_blast_SpellScript();
        }
};





void AddSC_custom_spell_scripts()
{
    RegisterSpellScript(spell_custom_health_gurad); //30秒护盾25%
    RegisterSpellScript(spell_divine_steed_charges); //骑士 神圣电驴
    RegisterSpellScript(aura_80020_expire_script); // 骑士 神圣电驴 2次光环
    RegisterSpellScript(spell_mage_alter_time_aura); //操控时间
    new spell_custom_double_jump(); //双跳
    new spell_custom_heroic_leap(); //大跳
    new spell_paladin_trigger_avenging_wrath(); // 触发复仇之怒
    new spell_modify_cooldown(); //百分比砍冷却时间
    new spell_rogue_feint(); //佯攻产生20%血量
    new spell_rogue_hemorrhage(); //出血产生40点能量
    new spell_rogue_stealth();//潜行5秒内 增加20%伤害。
    new spell_pri_mind_blast(); //心灵震爆造成的伤害提高75%，并且会对目标周围10码内的所有敌人造成伤害，同时为你恢复生命值，其数值相当于所造成伤害的25%。

    RegisterSpellScript(spell_mage_custom_damage); // 配合0.3秒触发一次的技能 这是触发的技能
}

/*
BeforeCast: 在魔法施放准备完成且施放处理之前调用。
OnCheckCast: 允许覆盖CheckCast函数的结果。
OnCast: 在魔法即将发射或执行时调用。
AfterCast: 在魔法发射后立即调用。
OnEffectLaunch: 在特定效果处理器调用之前调用，当魔法发射时。
OnEffectLaunchTarget: 对每个目标调用，类似于OnEffectLaunch。
OnEffectHit: 当魔法的效果命中目标时调用。
OnEffectHitTarget: 当魔法的效果命中每个目标时调用。
BeforeHit: 在魔法击中目标之前调用。
OnHit: 在魔法对目标造成伤害和触发光环之前调用。
AfterHit: 在魔法对目标的所有处理完成后调用。
OnObjectAreaTargetSelect: 在最终目标列表确定之前，对区域目标执行。
OnObjectTargetSelect: 在向最终目标列表添加单个单位目标之前执行。
OnDestinationTargetSelect: 在向最终目标列表添加目的地目标之前执行。

AuraScript 钩子
这些钩子用于控制光环的行为，包括应用、更新和移除光环：
DoCheckAreaTarget: 在区域光环检查是否可以应用于目标时执行。
OnDispel/AfterDispel: 当光环被驱散时调用。
OnEffectApply/AfterEffectApply: 在光环效果应用时调用。
OnEffectRemove/AfterEffectRemove: 在光环效果被移除时调用。
OnEffectPeriodic: 在周期性光环效果作用于目标时调用。
OnEffectUpdatePeriodic: 在更新周期性光环效果时调用。
DoEffectCalcAmount: 在计算光环效果数额时调用。
DoEffectCalcPeriodic: 在计算周期性光环数据时调用。
DoEffectCalcSpellMod: 在计算光环效果的法术修饰时调用。
OnEffectAbsorb/AfterEffectAbsorb: 当吸收型光环效果准备减少伤害时调用。
OnEffectManaShield/AfterEffectManaShield: 类似于吸收效果，专用于法力护盾。
OnEffectSplit: 当伤害分摊效果作用时调用。
*/