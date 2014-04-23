/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "Vehicle.h"
#include "Unit.h"
#include "Util.h"
#include "WorldPacket.h"
#include "ScriptMgr.h"
#include "CreatureAI.h"
#include "ZoneScript.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "MoveSplineInit.h"
#include "TemporarySummon.h"
#include "Player.h"
#include "EventProcessor.h"
#include "Player.h"
#include "Battleground.h"

Vehicle::Vehicle(Unit* unit, VehicleEntry const* vehInfo, uint32 creatureEntry) :
UsableSeatNum(0), _me(unit), _vehicleInfo(vehInfo), _creatureEntry(creatureEntry), _status(STATUS_NONE)
{
    for (uint32 i = 0; i < MAX_VEHICLE_SEATS; ++i)
    {
        if (uint32 seatId = _vehicleInfo->m_seatID[i])
            if (VehicleSeatEntry const* veSeat = sVehicleSeatStore.LookupEntry(seatId))
            {
                Seats.insert(std::make_pair(i, VehicleSeat(veSeat)));
                if (veSeat->CanEnterOrExit())
                    ++UsableSeatNum;
            }
    }

    // Set or remove correct flags based on available seats. Will overwrite db data (if wrong).
    if (UsableSeatNum)
        _me->SetFlag(UNIT_NPC_FLAGS, (_me->GetTypeId() == TYPEID_PLAYER ? UNIT_NPC_FLAG_PLAYER_VEHICLE : UNIT_NPC_FLAG_SPELLCLICK));
    else
        _me->RemoveFlag(UNIT_NPC_FLAGS, (_me->GetTypeId() == TYPEID_PLAYER ? UNIT_NPC_FLAG_PLAYER_VEHICLE : UNIT_NPC_FLAG_SPELLCLICK));

    InitMovementInfoForBase();
}

Vehicle::~Vehicle()
{
    /// @Uninstall must be called before this.
    ASSERT(_status == STATUS_UNINSTALLING);
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
    {
        if(!itr->second.Passenger)
            return;
    }
}

/**
 * @fn void Vehicle::Install()
 *
 * @brief Initializes power type for vehicle. Nothing more.
 *
 * @author Machiavelli
 * @date 17-2-2013
 */

void Vehicle::Install()
{
    if (Creature* creature = _me->ToCreature())
    {
        switch (_vehicleInfo->m_powerType)
        {
            case POWER_STEAM:
            case POWER_HEAT:
            case POWER_BLOOD:
            case POWER_OOZE:
            case POWER_WRATH:
            case POWER_ARCANE_ENERGY:
            case POWER_LIFE_ENERGY:
            case POWER_SUN_ENERGY:
            case POWER_SWING_VELOCITY:
            case POWER_SHADOWFLAME_ENERGY:
            case POWER_BLUE_POWER:
            case POWER_PURPLE_POWER:
            case POWER_GREEN_POWER:
            case POWER_ORANGE_POWER:
            case POWER_ELECTRICAL_ENERGY:
            case POWER_ARCANE_ENERGY_2:
            case POWER_FUEL:
            case POWER_SUN_POWER:
            case POWER_TWILIGHT_ENERGY:
                _me->setPowerType(POWER_ENERGY);
                _me->SetMaxPower(POWER_ENERGY, 100);
                break;
            case POWER_PYRITE:
                _me->setPowerType(POWER_ENERGY);
                _me->SetMaxPower(POWER_ENERGY, 50);
                break;
            case POWER_WIND_ROHASH:
            case POWER_WIND_ANSHAL:
            case POWER_WIND_NEZIR:
                _me->setPowerType(POWER_MANA);
                _me->SetMaxPower(POWER_MANA, 90);
                break;
            default:
                for (uint32 i = 0; i < MAX_SPELL_VEHICLE; ++i)
                {
                    if (!creature->m_spells[i])
                        continue;

                    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(creature->m_spells[i]);
                    if (!spellInfo)
                        continue;

                    if (spellInfo->PowerType == POWER_ENERGY)
                    {
                        _me->setPowerType(POWER_ENERGY);
                        _me->SetMaxPower(POWER_ENERGY, 100);
                        break;
                    }
                }
                break;
        }
        // Special Handling for SAI vehicle that should ignore pathfinding and other things
        switch (creature->GetEntry())
        {
            case 41848: // Mathel's Flying Machine
            case 42092: // Repaired Bomber
            case 42175: // Rixa's Flying Machine
            case 34400: // Thessera
            case 34375: // Ancient Grove Hippogryph
            case 34160: // Watch Wind Rider
            case 42693: // Vision of the Past
            case 48467: // Kasha
                creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
                creature->SetDisableGravity(true);
                creature->SetCanFly(true);
                creature->SetHover(true);
                break;
            case 52201: // Silver Bullet X-831
            case 34243: // Thessera (Twilight Zone)
            case 28670: // Frostbrood Vanquisher
            case 52385: // Burning Blade Flyer
            case 34322: // The Vortex
                break;
            case 44055: // Riverboat
            case 52316: // Mor'shan Caravan Cart
            case 45041: // Sylvanas Horse
            case 45057: // Sylvanas Horse 2
            case 52314: // Master Caravan Kodo
            case 52212: // Mor'shan Caravan Kodo
                creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
                break;
            case 42870: // Captured Lashtail Hatchling
                creature->SetControlled(true, UNIT_STATE_CANNOT_AUTOATTACK);
                creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                break;
            case 43241: // Wings of Hir'eek
                creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
                break;
            case 48395: // Orkus Camera
                creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
                break;
            case 48434: // Obsidian-Cloaked Dragon
                creature->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE);
                creature->SetControlled(true, UNIT_STATE_CANNOT_AUTOATTACK);
                creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
                creature->SetSpeed(MOVE_FLIGHT, 3.0f, true);
                creature->SetDisableGravity(true);
                creature->SetCanFly(true);
                creature->SetHover(true);
                break;
            case 46707: // Blam Turret
                creature->SetControlled(true, UNIT_STATE_ROOT);
                break;
            case 45245: // Depravity Event Camera
                creature->SetControlled(true, UNIT_STATE_CANNOT_AUTOATTACK);
                creature->AddUnitState(UNIT_STATE_IGNORE_PATHFINDING);
                break;
            case 45080: // Enthralled Val'kyr
                creature->SetControlled(true, UNIT_STATE_CANNOT_AUTOATTACK);
                break;
            default:
                break;
        }
    }

    _status = STATUS_INSTALLED;
    if (GetBase()->GetTypeId() == TYPEID_UNIT)
        sScriptMgr->OnInstall(this);
}

void Vehicle::InstallAllAccessories(bool evading)
{
    if (GetBase()->GetTypeId() == TYPEID_PLAYER || !evading)
        RemoveAllPassengers();   // We might have aura's saved in the DB with now invalid casters - remove

    VehicleAccessoryList const* accessories = sObjectMgr->GetVehicleAccessoryList(this);
    if (!accessories)
        return;

    for (VehicleAccessoryList::const_iterator itr = accessories->begin(); itr != accessories->end(); ++itr)
        if (!evading || itr->IsMinion)  // only install minions on evade mode
            InstallAccessory(itr->AccessoryEntry, itr->SeatId, itr->IsMinion, itr->SummonedType, itr->SummonTime);
}

/**
 * @fn void Vehicle::Uninstall()
 *
 * @brief Removes all passengers and sets status to STATUS_UNINSTALLING.
 *        No new passengers can be added to the vehicle after this call.
 *
 * @author Machiavelli
 * @date 17-2-2013
 */

void Vehicle::Uninstall()
{
    /// @Prevent recursive uninstall call. (Bad script in OnUninstall/OnRemovePassenger/PassengerBoarded hook.)
    if (_status == STATUS_UNINSTALLING && !GetBase()->HasUnitTypeMask(UNIT_MASK_MINION))
    {
        sLog->outError(LOG_FILTER_VEHICLES, "Vehicle GuidLow: %u, Entry: %u attempts to uninstall, but already has STATUS_UNINSTALLING! "
            "Check Uninstall/PassengerBoarded script hooks for errors.", _me->GetGUIDLow(), _me->GetEntry());
        return;
    }

    _status = STATUS_UNINSTALLING;
    sLog->outDebug(LOG_FILTER_VEHICLES, "Vehicle::Uninstall Entry: %u, GuidLow: %u", _creatureEntry, _me->GetGUIDLow());
    RemoveAllPassengers();

    if (GetBase()->GetTypeId() == TYPEID_UNIT)
        sScriptMgr->OnUninstall(this);
}

/**
 * @fn void Vehicle::Reset(bool evading )
 *
 * @brief Reapplies immunities and reinstalls accessories. Only has effect for creatures.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param evading true if called from CreatureAI::EnterEvadeMode
 */

void Vehicle::Reset(bool evading /*= false*/)
{
    if (GetBase()->GetTypeId() != TYPEID_UNIT)
        return;

    sLog->outDebug(LOG_FILTER_VEHICLES, "Vehicle::Reset (Entry: %u, GuidLow: %u, DBGuid: %u)", GetCreatureEntry(), _me->GetGUIDLow(), _me->ToCreature()->GetDBTableGUIDLow());

    ApplyAllImmunities();
    InstallAllAccessories(evading);

    sScriptMgr->OnReset(this);
}

/**
 * @fn void Vehicle::ApplyAllImmunities()
 *
 * @brief Applies specific immunities that cannot be set in DB.
 *
 * @author Machiavelli
 * @date 17-2-2013
 */

void Vehicle::ApplyAllImmunities()
{
    // This couldn't be done in DB, because some spells have MECHANIC_NONE

    // Vehicles should be immune on Knockback ...
    _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK, true);
    _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_KNOCK_BACK_DEST, true);

    // Mechanical units & vehicles ( which are not Bosses, they have own immunities in DB ) should be also immune on healing ( exceptions in switch below )
    if (_me->ToCreature() && _me->ToCreature()->GetCreatureTemplate()->type == CREATURE_TYPE_MECHANICAL && !_me->ToCreature()->isWorldBoss())
    {
        // Heal & dispel ...
        _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_HEAL, true);
        _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_HEAL_PCT, true);
        _me->ApplySpellImmune(0, IMMUNITY_EFFECT, SPELL_EFFECT_DISPEL, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_PERIODIC_HEAL, true);

        // ... Shield & Immunity grant spells ...
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_SCHOOL_IMMUNITY, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_UNATTACKABLE, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_SCHOOL_ABSORB, true);
        _me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_SHIELD, true);
        _me->ApplySpellImmune(0, IMMUNITY_MECHANIC, MECHANIC_IMMUNE_SHIELD, true);

        // ... Resistance, Split damage, Change stats ...
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_DAMAGE_SHIELD, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_SPLIT_DAMAGE_PCT, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_RESISTANCE, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_STAT, true);
        _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN, true);
    }

    // Different immunities for vehicles goes below
    switch (GetVehicleInfo()->m_ID)
    {
        // code below prevents a bug with movable cannons
        case 160: // Strand of the Ancients
        case 244: // Wintergrasp
        case 510: // Isle of Conquest
        case 470: // Rebel Cannon
        case 139: // Scarlet Cannon
        case 972: // Ol' Blasty
        case 1214: // Blam Turret
            _me->SetControlled(true, UNIT_STATE_ROOT);
            // why we need to apply this? we can simple add immunities to slow mechanic in DB
            _me->ApplySpellImmune(0, IMMUNITY_STATE, SPELL_AURA_MOD_DECREASE_SPEED, true);
            break;
        default:
            break;
    }
}

/**
 * @fn void Vehicle::RemoveAllPassengers()
 *
 * @brief Removes all current and pending passengers from the vehicle.
 *
 * @author Machiavelli
 * @date 17-2-2013
 */

void Vehicle::RemoveAllPassengers()
{
    sLog->outDebug(LOG_FILTER_VEHICLES, "Vehicle::RemoveAllPassengers. Entry: %u, GuidLow: %u", _creatureEntry, _me->GetGUIDLow());

    /// Setting to_Abort to true will cause @VehicleJoinEvent::Abort to be executed on next @Unit::UpdateEvents call
    /// This will properly "reset" the pending join process for the passenger.
    {
        /// Update vehicle pointer in every pending join event - Abort may be called after vehicle is deleted
        Vehicle* eventVehicle = _status != STATUS_UNINSTALLING ? this : NULL;

        while (!_pendingJoinEvents.empty())
        {
            VehicleJoinEvent* e = _pendingJoinEvents.front();
            e->to_Abort = true;
            e->Target = eventVehicle;
            _pendingJoinEvents.pop_front();
        }
    }

    // Passengers always cast an aura with SPELL_AURA_CONTROL_VEHICLE on the vehicle
    // We just remove the aura and the unapply handler will make the target leave the vehicle.
    // We don't need to iterate over Seats
    _me->RemoveAurasByType(SPELL_AURA_CONTROL_VEHICLE);

    // Following the above logic, this assertion should NEVER fail.
    // Even in 'hacky' cases, there should at least be VEHICLE_SPELL_RIDE_HARDCODED on us.
    // SeatMap::const_iterator itr;
    // for (itr = Seats.begin(); itr != Seats.end(); ++itr)
    //    ASSERT(!itr->second.passenger);
}

/**
 * @fn bool Vehicle::HasEmptySeat(int8 seatId) const
 *
 * @brief Checks if vehicle's seat specified by 'seatId' is empty.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param seatId Identifier for the seat.
 *
 * @return true if empty seat, false if not.
 */

bool Vehicle::HasEmptySeat(int8 seatId) const
{
    SeatMap::const_iterator seat = Seats.find(seatId);
    if (seat == Seats.end())
        return false;
    return !seat->second.Passenger;
}

/**
 * @fn Unit* Vehicle::GetPassenger(int8 seatId) const
 *
 * @brief Gets a passenger on specified seat.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param seatId Seat to look on.
 *
 * @return null if it not found, else pointer to passenger if in world
 */

Unit* Vehicle::GetPassenger(int8 seatId) const
{
    SeatMap::const_iterator seat = Seats.find(seatId);
    if (seat == Seats.end())
        return NULL;

    return ObjectAccessor::GetUnit(*GetBase(), seat->second.Passenger);
}

/**
 * @fn SeatMap::const_iterator Vehicle::GetNextEmptySeat(int8 seatId, bool next) const
 *
 * @brief Gets the next empty seat based on current seat.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param seatId Identifier for the current seat.
 * @param next   true if iterating forward, false means iterating backwards.
 *
 * @return The next empty seat.
 */

SeatMap::const_iterator Vehicle::GetNextEmptySeat(int8 seatId, bool next) const
{
    SeatMap::const_iterator seat = Seats.find(seatId);
    if (seat == Seats.end())
        return seat;

    while (seat->second.Passenger || (!seat->second.SeatInfo->CanEnterOrExit() && !seat->second.SeatInfo->IsUsableByOverride()))
    {
        if (next)
        {
            if (++seat == Seats.end())
                seat = Seats.begin();
        }
        else
        {
            if (seat == Seats.begin())
                seat = Seats.end();
            --seat;
        }

        // Make sure we don't loop indefinetly
        if (seat->first == seatId)
            return Seats.end();
    }

    return seat;
}

/**
 * @fn void Vehicle::InstallAccessory(uint32 entry, int8 seatId, bool minion, uint8 type,
 *     uint32 summonTime)
 *
 * @brief Installs an accessory.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param entry      The NPC entry of accessory.
 * @param seatId     Identifier for the seat to add the accessory to.
 * @param minion     true if accessory considered a 'minion'. Implies that the accessory will despawn when the vehicle despawns.
 *                   Essentially that it has no life without the vehicle. Their fates are bound.
 * @param type       See enum @SummonType.
 * @param summonTime Time after which the minion is despawned in case of a timed despawn @type specified.
 */

void Vehicle::InstallAccessory(uint32 entry, int8 seatId, bool minion, uint8 type, uint32 summonTime)
{
    /// @Prevent adding accessories when vehicle is uninstalling. (Bad script in OnUninstall/OnRemovePassenger/PassengerBoarded hook.)
    if (_status == STATUS_UNINSTALLING)
    {
        sLog->outError(LOG_FILTER_VEHICLES, "Vehicle (GuidLow: %u, DB GUID: %u, Entry: %u) attempts to install accessory (Entry: %u) on seat %d with STATUS_UNINSTALLING! "
            "Check Uninstall/PassengerBoarded script hooks for errors.", _me->GetGUIDLow(),
            (_me->GetTypeId() == TYPEID_UNIT ? _me->ToCreature()->GetDBTableGUIDLow() : _me->GetGUIDLow()), GetCreatureEntry(), entry, (int32)seatId);
        return;
    }

    sLog->outDebug(LOG_FILTER_VEHICLES, "Vehicle (GuidLow: %u, DB Guid: %u, Entry %u): installing accessory (Entry: %u) on seat: %d",
        _me->GetGUIDLow(), (_me->GetTypeId() == TYPEID_UNIT ? _me->ToCreature()->GetDBTableGUIDLow() : _me->GetGUIDLow()), GetCreatureEntry(),
        entry, (int32)seatId);

    TempSummon* accessory = _me->SummonCreature(entry, *_me, TempSummonType(type), summonTime);
    ASSERT(accessory);

    if (minion)
        accessory->AddUnitTypeMask(UNIT_MASK_ACCESSORY);

    (void)_me->HandleSpellClick(accessory, seatId);

    /// If for some reason adding accessory to vehicle fails it will unsummon in
    /// @VehicleJoinEvent::Abort
}

/**
 * @fn bool Vehicle::AddPassenger(Unit* unit, int8 seatId)
 *
 * @brief Attempts to add a passenger to the vehicle on 'seatId'.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param [in,out] The prospective passenger.
 * @param seatId        Identifier for the seat. Value of -1 indicates the next available seat.
 *
 * @return true if it succeeds, false if it fails.
 */

bool Vehicle::AddPassenger(Unit* unit, int8 seatId)
{
    /// @Prevent adding passengers when vehicle is uninstalling. (Bad script in OnUninstall/OnRemovePassenger/PassengerBoarded hook.)
    if (_status == STATUS_UNINSTALLING)
    {
        sLog->outError(LOG_FILTER_VEHICLES, "Passenger GuidLow: %u, Entry: %u, attempting to board vehicle GuidLow: %u, Entry: %u during uninstall! SeatId: %d",
            unit->GetGUIDLow(), unit->GetEntry(), _me->GetGUIDLow(), _me->GetEntry(), (int32)seatId);
        return false;
    }

    sLog->outDebug(LOG_FILTER_VEHICLES, "Unit %s scheduling enter vehicle (entry: %u, vehicleId: %u, guid: %u (dbguid: %u) on seat %d",
        unit->GetName().c_str(), _me->GetEntry(), _vehicleInfo->m_ID, _me->GetGUIDLow(),
        (_me->GetTypeId() == TYPEID_UNIT ? _me->ToCreature()->GetDBTableGUIDLow() : 0), (int32)seatId);

    // The seat selection code may kick other passengers off the vehicle.
    // While the validity of the following may be arguable, it is possible that when such a passenger
    // exits the vehicle will dismiss. That's why the actual adding the passenger to the vehicle is scheduled
    // asynchronously, so it can be cancelled easily in case the vehicle is uninstalled meanwhile.
    SeatMap::iterator seat;
    VehicleJoinEvent* e = new VehicleJoinEvent(this, unit);
    unit->m_Events.AddEvent(e, unit->m_Events.CalculateTime(0));

    if (seatId < 0) // no specific seat requirement
    {
        for (seat = Seats.begin(); seat != Seats.end(); ++seat)
            if (!seat->second.Passenger && (seat->second.SeatInfo->CanEnterOrExit() || seat->second.SeatInfo->IsUsableByOverride()))
                break;

        if (seat == Seats.end()) // no available seat
        {
            e->to_Abort = true;
            return false;
        }

        e->Seat = seat;
        _pendingJoinEvents.push_back(e);
    }
    else
    {
        seat = Seats.find(seatId);
        if (seat == Seats.end())
        {
            e->to_Abort = true;
            return false;
        }

        e->Seat = seat;
        _pendingJoinEvents.push_back(e);
        if (seat->second.Passenger)
        {
            Unit* passenger = ObjectAccessor::GetUnit(*GetBase(), seat->second.Passenger);
            ASSERT(passenger);
            passenger->ExitVehicle();
        }

        ASSERT(!seat->second.Passenger);
    }

    return true;
}

/**
 * @fn void Vehicle::RemovePassenger(Unit* unit)
 *
 * @brief Removes the passenger from the vehicle.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param [in,out] unit The passenger to remove.
 */

void Vehicle::RemovePassenger(Unit* unit)
{
    if (unit->GetVehicle() != this)
        return;

    SeatMap::iterator seat = GetSeatIteratorForPassenger(unit);
    ASSERT(seat != Seats.end());

    sLog->outDebug(LOG_FILTER_VEHICLES, "Unit %s exit vehicle entry %u id %u dbguid %u seat %d",
        unit->GetName().c_str(), _me->GetEntry(), _vehicleInfo->m_ID, _me->GetGUIDLow(), (int32)seat->first);

    seat->second.Passenger = 0;
    if (seat->second.SeatInfo->CanEnterOrExit() && ++UsableSeatNum)
        _me->SetFlag(UNIT_NPC_FLAGS, (_me->GetTypeId() == TYPEID_PLAYER ? UNIT_NPC_FLAG_PLAYER_VEHICLE : UNIT_NPC_FLAG_SPELLCLICK));

    unit->ClearUnitState(UNIT_STATE_ONVEHICLE);

    if (_me->GetTypeId() == TYPEID_UNIT && unit->GetTypeId() == TYPEID_PLAYER && seat->second.SeatInfo->m_flags & VEHICLE_SEAT_FLAG_CAN_CONTROL)
        _me->RemoveCharmedBy(unit);

    if (_me->IsInWorld())
    {
        unit->m_movementInfo.t_pos.Relocate(0.0f, 0.0f, 0.0f, 0.0f);
        unit->m_movementInfo.t_time = 0;
        unit->m_movementInfo.t_seat = 0;
    }

    // only for flyable vehicles
    if (unit->IsFlying())
        if (_me->IsFriendlyTo(unit))
            _me->CastSpell(unit, VEHICLE_SPELL_PARACHUTE, true);

    if (_me->GetTypeId() == TYPEID_UNIT && _me->ToCreature()->IsAIEnabled)
        _me->ToCreature()->AI()->PassengerBoarded(unit, seat->first, false);

    if (GetBase()->GetTypeId() == TYPEID_UNIT)
        sScriptMgr->OnRemovePassenger(this, unit);
}

/**
 * @fn void Vehicle::RelocatePassengers()
 *
 * @brief Relocate passengers. Must be called after m_base::Relocate
 *
 * @author Machiavelli
 * @date 17-2-2013
 */

void Vehicle::RelocatePassengers()
{
    ASSERT(_me->GetMap());

    // not sure that absolute position calculation is correct, it must depend on vehicle pitch angle
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
    {
        if (Unit* passenger = ObjectAccessor::GetUnit(*GetBase(), itr->second.Passenger))
        {
            ASSERT(passenger->IsInWorld());

            float px, py, pz, po;
            passenger->m_movementInfo.t_pos.GetPosition(px, py, pz, po);
            CalculatePassengerPosition(px, py, pz, po);

            passenger->UpdatePosition(px, py, pz, po);
        }
    }
}

/**
 * @fn bool Vehicle::IsVehicleInUse() const
 *
 * @brief Returns information whether the vehicle is currently used by any unit
 *
 * @author Shauren
 * @date 26-2-2013
 *
 * @return true if any passenger is boarded on vehicle, false otherwise.
 */

bool Vehicle::IsVehicleInUse() const
{
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (itr->second.Passenger)
            return true;

    return false;
}

/**
 * @fn void Vehicle::InitMovementInfoForBase()
 *
 * @brief Sets correct MovementFlags2 based on VehicleFlags from DBC.
 *
 * @author Machiavelli
 * @date 17-2-2013
 */

void Vehicle::InitMovementInfoForBase()
{
    uint32 vehicleFlags = GetVehicleInfo()->m_flags;

    if (vehicleFlags & VEHICLE_FLAG_NO_STRAFE)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_NO_STRAFE);
    if (vehicleFlags & VEHICLE_FLAG_NO_JUMPING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_NO_JUMPING);

    /* EXPERIMENTAL FOR BOATS */
    if (_me->GetEntry() == 40854)
    {
         _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_NO_JUMPING);
         _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_NO_STRAFE);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_PITCH_UP);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_PITCH_DOWN);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING_FAR);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_FORWARD);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_BACKWARD);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_SPLINE_ELEVATION);
         _me->RemoveUnitMovementFlag(MOVEMENTFLAG_SWIMMING);
         _me->SetUnitMovementFlags(MOVEMENTFLAG_WALKING);
         _me->SetSpeed(MOVE_SWIM, 4.5f, true);
         _me->SetSpeed(MOVE_FLIGHT, 4.5f, true);
         _me->SetSpeed(MOVE_RUN, 4.5f, true);
         _me->SetSpeed(MOVE_SWIM_BACK, 0.001f, true);
         _me->SetSpeed(MOVE_FLIGHT_BACK, 0.001f, true);
         _me->SetSpeed(MOVE_RUN_BACK, 0.001f, true);
         _me->SetSpeed(MOVE_PITCH_RATE, 0.10f,true);
         return;
    }

    if (vehicleFlags & VEHICLE_FLAG_FULLSPEEDTURNING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_FULL_SPEED_TURNING);
    if (vehicleFlags & VEHICLE_FLAG_ALLOW_PITCHING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_ALWAYS_ALLOW_PITCHING);
    if (vehicleFlags & VEHICLE_FLAG_FULLSPEEDPITCHING)
        _me->AddExtraUnitMovementFlag(MOVEMENTFLAG2_FULL_SPEED_PITCHING);
}

/**
 * @fn VehicleSeatEntry const* Vehicle::GetSeatForPassenger(Unit* passenger)
 *
 * @brief Returns information on the seat of specified passenger, represented by the format in VehicleSeat.dbc
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param [in,out] The passenger for which we check the seat info.
 *
 * @return null if passenger not found on vehicle, else the DBC record for the seat.
 */

VehicleSeatEntry const* Vehicle::GetSeatForPassenger(Unit const* passenger) const
{
    for (SeatMap::const_iterator itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (itr->second.Passenger == passenger->GetGUID())
            return itr->second.SeatInfo;

    return NULL;
}

/**
 * @fn SeatMap::iterator Vehicle::GetSeatIteratorForPassenger(Unit* passenger)
 *
 * @brief Gets seat iterator for specified passenger.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param [in,out] passenger Passenger to look up.
 *
 * @return The seat iterator for specified passenger if it's found on the vehicle. Otherwise Seats.end() (invalid iterator).
 */

SeatMap::iterator Vehicle::GetSeatIteratorForPassenger(Unit* passenger)
{
    SeatMap::iterator itr;
    for (itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (itr->second.Passenger == passenger->GetGUID())
            return itr;

    return Seats.end();
}

/**
 * @fn uint8 Vehicle::GetAvailableSeatCount() const
 *
 * @brief Gets the available seat count.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @return The available seat count.
 */

uint8 Vehicle::GetAvailableSeatCount() const
{
    uint8 ret = 0;
    SeatMap::const_iterator itr;
    for (itr = Seats.begin(); itr != Seats.end(); ++itr)
        if (!itr->second.Passenger && (itr->second.SeatInfo->CanEnterOrExit() || itr->second.SeatInfo->IsUsableByOverride()))
            ++ret;

    return ret;
}

void Vehicle::CalculatePassengerPosition(float& x, float& y, float& z, float& o)
{
    float inx = x, iny = y, inz = z, ino = o;
    o = GetBase()->GetOrientation() + ino;
    x = GetBase()->GetPositionX() + inx * std::cos(GetBase()->GetOrientation()) - iny * std::sin(GetBase()->GetOrientation());
    y = GetBase()->GetPositionY() + iny * std::cos(GetBase()->GetOrientation()) + inx * std::sin(GetBase()->GetOrientation());
    z = GetBase()->GetPositionZ() + inz;
}

void Vehicle::CalculatePassengerOffset(float& x, float& y, float& z, float& o)
{
    o -= GetBase()->GetOrientation();
    z -= GetBase()->GetPositionZ();
    y -= GetBase()->GetPositionY();    // y = searchedY * std::cos(o) + searchedX * std::sin(o)
    x -= GetBase()->GetPositionX();    // x = searchedX * std::cos(o) + searchedY * std::sin(o + pi)
    float inx = x, iny = y;
    y = (iny - inx * tan(GetBase()->GetOrientation())) / (cos(GetBase()->GetOrientation()) + std::sin(GetBase()->GetOrientation()) * tan(GetBase()->GetOrientation()));
    x = (inx + iny * tan(GetBase()->GetOrientation())) / (cos(GetBase()->GetOrientation()) + std::sin(GetBase()->GetOrientation()) * tan(GetBase()->GetOrientation()));
}

/**
 * @fn void Vehicle::RemovePendingEvent(VehicleJoinEvent* e)
 *
 * @brief Removes @VehicleJoinEvent objects from pending join event store.
 *        This method only removes it after it's executed or aborted to prevent leaving
 *        pointers to deleted events.
 *
 * @author Shauren
 * @date 22-2-2013
 *
 * @param [in] e The VehicleJoinEvent* to remove from pending event store.
 */

void Vehicle::RemovePendingEvent(VehicleJoinEvent* e)
{
    for (PendingJoinEventContainer::iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end(); ++itr)
    {
        if (*itr == e)
        {
            _pendingJoinEvents.erase(itr);
            break;
        }
    }
}

/**
 * @fn void Vehicle::RemovePendingEventsForSeat(uint8 seatId)
 *
 * @brief Removes any pending events for given seatId. Executed when a @VehicleJoinEvent::Execute is called
 *
 * @author Machiavelli
 * @date 23-2-2013
 *
 * @param seatId Identifier for the seat.
 */

void Vehicle::RemovePendingEventsForSeat(int8 seatId)
{
    for (PendingJoinEventContainer::iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end();)
    {
        if ((*itr)->Seat->first == seatId)
        {
            (*itr)->to_Abort = true;
            _pendingJoinEvents.erase(itr++);
        }
        else
            ++itr;
    }
}

/**
 * @fn void Vehicle::RemovePendingEventsForSeat(uint8 seatId)
 *
 * @brief Removes any pending events for given passenger. Executed when vehicle control aura is removed while adding passenger is in progress
 *
 * @author Shauren
 * @date 13-2-2013
 *
 * @param passenger Unit that is supposed to enter the vehicle.
 */

void Vehicle::RemovePendingEventsForPassenger(Unit* passenger)
{
    for (PendingJoinEventContainer::iterator itr = _pendingJoinEvents.begin(); itr != _pendingJoinEvents.end();)
    {
        if ((*itr)->Passenger == passenger)
        {
            (*itr)->to_Abort = true;
            _pendingJoinEvents.erase(itr++);
        }
        else
            ++itr;
    }
}

VehicleJoinEvent::~VehicleJoinEvent()
{
    if (Target)
        Target->RemovePendingEvent(this);
}

/**
 * @fn bool VehicleJoinEvent::Execute(uint64, uint32)
 *
 * @brief Actually adds the passenger @Passenger to vehicle @Target.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param parameter1 Unused
 * @param parameter2 Unused.
 *
 * @return true, cannot fail.
 *
 */

bool VehicleJoinEvent::Execute(uint64, uint32)
{
    ASSERT(Passenger->IsInWorld());
    ASSERT(Target && Target->GetBase()->IsInWorld());
    ASSERT(Target->GetBase()->HasAuraTypeWithCaster(SPELL_AURA_CONTROL_VEHICLE, Passenger->GetGUID()));

    Target->RemovePendingEventsForSeat(Seat->first);

    Passenger->m_vehicle = Target;
    Seat->second.Passenger = Passenger->GetGUID();
    if (Seat->second.SeatInfo->CanEnterOrExit())
    {
        ASSERT(Target->UsableSeatNum);
        --(Target->UsableSeatNum);
        if (!Target->UsableSeatNum)
        {
            if (Target->GetBase()->GetTypeId() == TYPEID_PLAYER)
                Target->GetBase()->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_PLAYER_VEHICLE);
            else
                Target->GetBase()->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_SPELLCLICK);
        }
    }

    Passenger->InterruptNonMeleeSpells(false);
    Passenger->RemoveAurasByType(SPELL_AURA_MOUNTED);

    Player* player = Passenger->ToPlayer();
    if (player)
    {
        // drop flag
        if (Battleground* bg = player->GetBattleground())
            bg->EventPlayerDroppedFlag(player);

        player->StopCastingCharm();
        player->StopCastingBindSight();
        player->SendOnCancelExpectedVehicleRideAura();
        player->TemporaryUnsummonPet();
    }

    if (Seat->second.SeatInfo->m_flags && !(Seat->second.SeatInfo->m_flags & VEHICLE_SEAT_FLAG_ALLOW_TURNING))
        Passenger->AddUnitState(UNIT_STATE_ONVEHICLE);

    // Some creatures should be attackable while on vehicle, so let's make a special switch for they
    if (Passenger->GetTypeId() != TYPEID_PLAYER && Passenger->ToCreature())
    {
        switch (Passenger->ToCreature()->GetEntry())
        {
            case 34487: // Razormane Raider
            case 34594: // Burning Blade Raider
                Passenger->ClearUnitState(UNIT_STATE_ONVEHICLE);
                break;
            default:
                break;
        }
    }

    VehicleSeatEntry const* veSeat = Seat->second.SeatInfo;
    Passenger->m_movementInfo.t_pos.Relocate(veSeat->m_attachmentOffsetX, veSeat->m_attachmentOffsetY, veSeat->m_attachmentOffsetZ);
    Passenger->m_movementInfo.t_time = 0; // 1 for player
    Passenger->m_movementInfo.t_seat = Seat->first;
    Passenger->m_movementInfo.t_guid = Target->GetBase()->GetGUID();

    if (Target->GetBase()->GetTypeId() == TYPEID_UNIT && Passenger->GetTypeId() == TYPEID_PLAYER &&
        Seat->second.SeatInfo->m_flags & VEHICLE_SEAT_FLAG_CAN_CONTROL)
        ASSERT(Target->GetBase()->SetCharmedBy(Passenger, CHARM_TYPE_VEHICLE))  // SMSG_CLIENT_CONTROL

    Passenger->SendClearTarget();                            // SMSG_BREAK_TARGET
    Passenger->SendGravityDisable();
    Passenger->SetControlled(true, UNIT_STATE_ROOT);         // SMSG_FORCE_ROOT - In some cases we send SMSG_SPLINE_MOVE_ROOT here (for creatures)
    // also adds MOVEMENTFLAG_ROOT

    Movement::MoveSplineInit init(Passenger);
    init.DisableTransportPathTransformations();
    init.MoveTo(veSeat->m_attachmentOffsetX, veSeat->m_attachmentOffsetY, veSeat->m_attachmentOffsetZ, false, true);
    init.SetFacing(0.0f);
    init.SetTransportEnter();
    init.Launch();

    if (Target->GetBase()->GetTypeId() == TYPEID_UNIT)
    {
        if (Target->GetBase()->ToCreature()->IsAIEnabled)
            Target->GetBase()->ToCreature()->AI()->PassengerBoarded(Passenger, Seat->first, true);

        sScriptMgr->OnAddPassenger(Target, Passenger, Seat->first);

        // Actually quite a redundant hook. Could just use OnAddPassenger and check for unit typemask inside script.
        if (Passenger->HasUnitTypeMask(UNIT_MASK_ACCESSORY))
            sScriptMgr->OnInstallAccessory(Target, Passenger->ToCreature());
    }

    return true;
}

/**
 * @fn void VehicleJoinEvent::Abort(uint64)
 *
 * @brief Aborts the event. Implies that unit @Passenger will not be boarding vehicle @Target after all.
 *
 * @author Machiavelli
 * @date 17-2-2013
 *
 * @param parameter1 Unused
 */

void VehicleJoinEvent::Abort(uint64)
{
    /// Check if the Vehicle was already uninstalled, in which case all auras were removed already
    if (Target)
    {
        sLog->outDebug(LOG_FILTER_VEHICLES, "Passenger GuidLow: %u, Entry: %u, board on vehicle GuidLow: %u, Entry: %u SeatId: %d cancelled",
            Passenger->GetGUIDLow(), Passenger->GetEntry(), Target->GetBase()->GetGUIDLow(), Target->GetBase()->GetEntry(), (int32)Seat->first);

        /// @SPELL_AURA_CONTROL_VEHICLE auras can be applied even when the passenger is not (yet) on the vehicle.
        /// When this code is triggered it means that something went wrong in @Vehicle::AddPassenger, and we should remove
        /// the aura manually.
        Target->GetBase()->RemoveAurasByType(SPELL_AURA_CONTROL_VEHICLE, Passenger->GetGUID());
    }
    else
        sLog->outDebug(LOG_FILTER_VEHICLES, "Passenger GuidLow: %u, Entry: %u, board on uninstalled vehicle SeatId: %d cancelled",
            Passenger->GetGUIDLow(), Passenger->GetEntry(), (int32)Seat->first);

    if (Passenger->IsInWorld() && Passenger->HasUnitTypeMask(UNIT_MASK_ACCESSORY))
        Passenger->ToCreature()->DespawnOrUnsummon();
}
