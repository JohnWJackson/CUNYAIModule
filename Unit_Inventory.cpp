#pragma once
#define _MOVING_AVERAGE_DURATION 96 // set MA duration, usually 96 frames

#include <BWAPI.h>
#include "Source\CUNYAIModule.h"
#include "Source\Unit_Inventory.h"
#include "Source\InventoryManager.h"
#include "Source\Reservation_Manager.h"
#include "Source\FAP\include\FAP.hpp" // could add to include path but this is more explicit.
#include <random> // C++ base random is low quality.
#include <fstream>

//Unit_Inventory functions.
//Creates an instance of the unit inventory class.
Unit_Inventory::Unit_Inventory(){}

Unit_Inventory::Unit_Inventory( const Unitset &unit_set) {

	for (const auto & u : unit_set) {
		unit_inventory_.insert({ u, Stored_Unit(u) });
	}

    updateUnitInventorySummary(); //this call is a CPU sink.
}

void Unit_Inventory::updateUnitInventory(const Unitset &unit_set){
    for (const auto & u : unit_set) {
        Stored_Unit* found_unit = getStoredUnit(u);
        if (found_unit) {
            found_unit->updateStoredUnit(u); // explicitly does not change locked mineral.
        }
        else {
            unit_inventory_.insert({ u, Stored_Unit(u) });
        }
    }
    updateUnitInventorySummary(); //this call is a CPU sink.
}

void Unit_Inventory::updateUnitsControlledByOthers()
{
    for (auto &e: unit_inventory_) {
        if (e.second.bwapi_unit_ && e.second.bwapi_unit_->exists()) { // If the unit is visible now, update its position.
            e.second.updateStoredUnit(e.second.bwapi_unit_);
        }
        else if (Broodwar->isVisible(TilePosition(e.second.pos_))) {  // if you can see the tile it SHOULD be at Burned down buildings will pose a problem in future.

            bool present = false;

            Unitset enemies_tile = Broodwar->getUnitsOnTile(TilePosition(e.second.pos_), IsEnemy || IsNeutral);  // Confirm it is present.  Main use: Addons convert to neutral if their main base disappears, extractors.
            for (auto &et : enemies_tile ) {
                present = et->getID() == e.second.unit_ID_ /*|| (*et)->isCloaked() || (*et)->isBurrowed()*/;
                if (present) {
                    e.second.updateStoredUnit(et);
                    break;
                }
            }
            if (!present && e.second.valid_pos_ && e.second.type_.canMove()) { // If the last known position is visible, and the unit is not there, then they have an unknown position.  Note a variety of calls to e->first cause crashes here. Let us make a linear projection of their position 24 frames (1sec) into the future.
                Position potential_running_spot = Position(e.second.pos_.x + e.second.velocity_x_, e.second.pos_.y + e.second.velocity_y_);
                if (!potential_running_spot.isValid() || Broodwar->isVisible(TilePosition(potential_running_spot))) {
                    e.second.valid_pos_ = false;
                }
                else if (potential_running_spot.isValid() && !Broodwar->isVisible(TilePosition(potential_running_spot)) &&
                    (e.second.type_.isFlyer() || Broodwar->isWalkable(WalkPosition(potential_running_spot)))) {
                    e.second.pos_ = potential_running_spot;
                    e.second.valid_pos_ = true;
                }
            }
        }

        e.second.circumference_remaining_ = e.second.circumference_; //if we update the unit, give it back its circumfrance. This may lead to every frame the unit being considered unsurrounded.  Tracking every single target and updating is not yet implemented but could be eventually.

        if ((e.second.type_ == UnitTypes::Resource_Vespene_Geyser) || e.second.type_ == UnitTypes::Unknown ) { // Destroyed refineries revert to geyers, requiring the manual catch. Unknowns should be removed as well.
            e.second.valid_pos_ = false;
        }
    }
}

void Unit_Inventory::purgeBrokenUnits()
{
    for (auto &e = this->unit_inventory_.begin(); e != this->unit_inventory_.end() && !this->unit_inventory_.empty(); ) {
        if ((e->second.type_ == UnitTypes::Resource_Vespene_Geyser) || // Destroyed refineries revert to geyers, requiring the manual catc.
            e->second.type_ == UnitTypes::None) { // sometimes they have a "none" in inventory. This isn't very reasonable, either.
            e = this->unit_inventory_.erase(e); // get rid of these. Don't iterate if this occurs or we will (at best) end the loop with an invalid iterator.
        }
        else {
            ++e;
        }
    }
}

void Unit_Inventory::purgeUnseenUnits()
{
    for (auto &f = this->unit_inventory_.begin(); f != this->unit_inventory_.end() && !this->unit_inventory_.empty(); ) {
        if (!f->second.bwapi_unit_ || !f->second.bwapi_unit_->exists()) { // sometimes they have a "none" in inventory. This isn't very reasonable, either.
            f = this->unit_inventory_.erase(f); // get rid of these. Don't iterate if this occurs or we will (at best) end the loop with an invalid iterator.
        }
        else {
            ++f;
        }
    }
}


// Decrements all resources worker was attached to, clears all reservations associated with that worker. Stops Unit.
void Unit_Inventory::purgeWorkerRelations(const Unit &unit, Resource_Inventory &ri, Inventory &inv, Reservation &res)
{
    UnitCommand command = unit->getLastCommand();
    Stored_Unit& miner = this->unit_inventory_.find(unit)->second;
    miner.stopMine(ri);

    if (command.getType() == UnitCommandTypes::Morph || command.getType() == UnitCommandTypes::Build ) {
        res.removeReserveSystem(unit->getBuildType());
    }
    if (command.getTargetPosition() == Position(inv.next_expo_) ) {
        res.removeReserveSystem( UnitTypes::Zerg_Hatchery );
    }
    unit->stop();
    miner.time_of_last_purge_ = Broodwar->getFrameCount();
    miner.updateStoredUnit(unit);
}

// Decrements all resources worker was attached to, clears all reservations associated with that worker. Stops Unit.
void Unit_Inventory::purgeWorkerRelationsNoStop(const Unit &unit, Resource_Inventory &ri, Inventory &inv, Reservation &res)
{
    UnitCommand command = unit->getLastCommand();
    Stored_Unit& miner = this->unit_inventory_.find(unit)->second;
    miner.stopMine(ri);

    if (command.getType() == UnitCommandTypes::Morph || command.getType() == UnitCommandTypes::Build) {
        res.removeReserveSystem(unit->getBuildType());
    }
    if (command.getTargetPosition() == Position(inv.next_expo_)) {
        res.removeReserveSystem(UnitTypes::Zerg_Hatchery);
    }
    miner.time_of_last_purge_ = Broodwar->getFrameCount();
    miner.updateStoredUnit(unit);
}

void Unit_Inventory::drawAllVelocities(const Inventory &inv) const
{
    for (auto u : unit_inventory_) {
        Position destination = Position(u.second.pos_.x + u.second.velocity_x_ * 24, u.second.pos_.y + u.second.velocity_y_ * 24);
        CUNYAIModule::Diagnostic_Line(u.second.pos_, destination, inv.screen_position_, Colors::Green);
    }
}

void Unit_Inventory::drawAllHitPoints(const Inventory &inv) const
{
    for (auto u : unit_inventory_) {
        CUNYAIModule::DiagnosticHitPoints(u.second, inv.screen_position_);
    }

}
void Unit_Inventory::drawAllMAFAPaverages(const Inventory &inv) const
{
    for (auto u : unit_inventory_) {
        CUNYAIModule::DiagnosticFAP(u.second, inv.screen_position_);
    }

}

void Unit_Inventory::drawAllSpamGuards(const Inventory &inv) const
{
    for (auto u : unit_inventory_) {
        CUNYAIModule::DiagnosticSpamGuard(u.second, inv.screen_position_);
    }
}

void Unit_Inventory::drawAllWorkerTasks(const Inventory & inv, Resource_Inventory &ri) const
{
    for (auto u : unit_inventory_) {
        if (u.second.type_ == UnitTypes::Zerg_Drone) {
            if (u.second.locked_mine_ && !u.second.isAssignedResource(ri) && !u.second.isAssignedClearing(ri)) {
                CUNYAIModule::Diagnostic_Line(u.second.pos_, u.second.locked_mine_->getPosition(), inv.screen_position_, Colors::White);
            }
            else if (u.second.isAssignedMining(ri)) {
                CUNYAIModule::Diagnostic_Line(u.second.pos_, u.second.locked_mine_->getPosition(), inv.screen_position_, Colors::Green);
            }
            else if (u.second.isAssignedGas(ri)) {
                CUNYAIModule::Diagnostic_Line(u.second.pos_, u.second.locked_mine_->getPosition(), inv.screen_position_, Colors::Brown);
            }
            else if (u.second.isAssignedClearing(ri)) {
                CUNYAIModule::Diagnostic_Line(u.second.pos_, u.second.locked_mine_->getPosition(), inv.screen_position_, Colors::Blue);
            }

            if (u.second.isAssignedBuilding(ri)) {
                CUNYAIModule::Diagnostic_Dot(u.second.pos_, inv.screen_position_, Colors::Purple);
            }
        }
    }
}

void Unit_Inventory::drawAllLocations(const Inventory & inv) const
{
    if (_ANALYSIS_MODE) {
        for (auto e = unit_inventory_.begin(); e != unit_inventory_.end() && !unit_inventory_.empty(); e++) {
            if (CUNYAIModule::isOnScreen(e->second.pos_, inv.screen_position_)) {
                if (e->second.valid_pos_) {
                    Broodwar->drawCircleMap(e->second.pos_, (e->second.type_.dimensionUp() + e->second.type_.dimensionLeft()) / 2, Colors::Red); // Plot their last known position.
                }
                else if (!e->second.valid_pos_) {
                    Broodwar->drawCircleMap(e->second.pos_, (e->second.type_.dimensionUp() + e->second.type_.dimensionLeft()) / 2, Colors::Blue); // Plot their last known position.
                }
            }
        }
    }
}

// Updates the count of units.
void Unit_Inventory::addStored_Unit( const Unit &unit ) {
    unit_inventory_.insert( { unit, Stored_Unit( unit ) } );
};

void Unit_Inventory::addStored_Unit( const Stored_Unit &stored_unit ) {
    unit_inventory_.insert( { stored_unit.bwapi_unit_ , stored_unit } );
};

void Stored_Unit::updateStoredUnit(const Unit &unit){

    valid_pos_ = true;
    pos_ = unit->getPosition();
    build_type_ = unit->getBuildType();
    shields_ = unit->getShields();
    health_ = unit->getHitPoints();
    current_hp_ = shields_ + health_;
    velocity_x_ = unit->getVelocityX();
    velocity_y_ = unit->getVelocityY();
    order_ = unit->getOrder();
    command_ = unit->getLastCommand();
    time_since_last_command_ = Broodwar->getFrameCount() - unit->getLastCommandFrame();

    //Needed for FAP.
    is_flying_ = unit->isFlying();
    elevation_ = BWAPI::Broodwar->getGroundHeight(TilePosition(pos_));
    cd_remaining_ = unit->getAirWeaponCooldown();
    stimmed_ = unit->isStimmed();

    if (type_ != unit->getType()) {
        type_ = unit->getType();
        stock_value_ = Stored_Unit(type_).stock_value_; // longer but prevents retyping.
        circumference_ = type_.height() * 2 + type_.width() * 2;
        circumference_remaining_ = circumference_;
        future_fap_value_ = stock_value_;
        current_stock_value_ = (int)(stock_value_ * current_hp_ / (double)(type_.maxHitPoints() + type_.maxShields())); 
        ma_future_fap_value_ = stock_value_;
    }
    else {
        double weight = (_MOVING_AVERAGE_DURATION - 1) / (double)_MOVING_AVERAGE_DURATION;
        circumference_remaining_ = circumference_;
        current_stock_value_ = (int)(stock_value_ * current_hp_ / (double)(type_.maxHitPoints() + type_.maxShields())); 
        ma_future_fap_value_ = (double)(1 - weight) * ma_future_fap_value_ + weight * future_fap_value_;
    }
}

//Removes units that have died
void Unit_Inventory::removeStored_Unit( Unit e_unit ) {
    unit_inventory_.erase( e_unit );
};


 Position Unit_Inventory::getMeanLocation() const {
    int x_sum = 0;
    int y_sum = 0;
    int count = 0;
    Position out = Position(0,0);
    for ( const auto &u : this->unit_inventory_ ) {
        if (u.second.valid_pos_) {
            x_sum += u.second.pos_.x;
            y_sum += u.second.pos_.y;
            count++;
        }
    }
    if ( count > 0 ) {
        out = Position( x_sum / count, y_sum / count );
    }
    return out;
}

 Position Unit_Inventory::getMeanBuildingLocation() const {
     int x_sum = 0;
     int y_sum = 0;
     int count = 0;
     for ( const auto &u : this->unit_inventory_ ) {
         if ( ( (u.second.type_.isBuilding() && !u.second.type_.isSpecialBuilding()) || u.second.bwapi_unit_->isMorphing() ) && u.second.valid_pos_ ) {
             x_sum += u.second.pos_.x;
             y_sum += u.second.pos_.y;
             count++;
         }
     }
     if ( count > 0 ) {
         Position out = { x_sum / count, y_sum / count };
         return out;
     }
     else {
         return Position( 0, 0 ); // you're dead at this point, fyi.
     }
 }

// In progress
 Position Unit_Inventory::getStrongestLocation() const {
     int x_sum = 0;
     int y_sum = 0;
     int count = 0;
     int standard_value = Stored_Unit(UnitTypes::Zerg_Drone).stock_value_;
     for (const auto &u : this->unit_inventory_) {
         if (CUNYAIModule::IsFightingUnit(u.second) && u.second.valid_pos_) {
             int remaining_stock = u.second.current_stock_value_;
                 while (remaining_stock > 0) {
                     x_sum += u.second.pos_.x;
                     y_sum += u.second.pos_.y;
                     count++;
                     remaining_stock -= standard_value;
                 }
         }
     }
     if (count > 0) {
         Position out = { x_sum / count, y_sum / count };
         return out;
     }
     else {
         return Position(0, 0); // you're dead at this point, fyi.
     }
 }


 Position Unit_Inventory::getMeanCombatLocation() const {
     int x_sum = 0;
     int y_sum = 0;
     int count = 0;
     for ( const auto &u : this->unit_inventory_) {
         if ( u.second.type_.canAttack() && u.second.valid_pos_ ) {
             x_sum += u.second.pos_.x;
             y_sum += u.second.pos_.y;
             count++;
         }
     }
     if ( count > 0 ) {
         Position out = { x_sum / count, y_sum / count };
         return out;
     }
     else {
         return Position( 0, 0 );  // you might be dead at this point, fyi.
     }

 }

 //for the army that can actually move.
 Position Unit_Inventory::getMeanArmyLocation() const {
     int x_sum = 0;
     int y_sum = 0;
     int count = 0;
     for (const auto &u : this->unit_inventory_) {
         if (u.second.type_.canAttack() && u.second.valid_pos_ && u.second.type_.canMove() && !u.second.type_.isWorker() ) {
             x_sum += u.second.pos_.x;
             y_sum += u.second.pos_.y;
             count++;
         }
     }
     if (count > 0) {
         Position out = { x_sum / count, y_sum / count };
         return out;
     }
     else {
         return Position(0, 0);  // you might be dead at this point, fyi.
     }
 }

 //for the army that can actually move. Removed for usage of Broodwar->getclosest(), a very slow function.
 //Position Unit_Inventory::getClosestMeanArmyLocation() const {
 //    Position mean_pos = getMeanArmyLocation();
 //    if( mean_pos && mean_pos != Position(0,0) && mean_pos.isValid()){
 //       Unit nearest_neighbor = Broodwar->getClosestUnit(mean_pos, !IsFlyer && IsOwned, 500);
 //        if (nearest_neighbor && nearest_neighbor->getPosition() ) {
 //            Position out = Broodwar->getClosestUnit(mean_pos, !IsFlyer && IsOwned, 500)->getPosition();
 //            return out;
 //        }
 //        else {
 //            return Position(0, 0);  // you might be dead at this point, fyi.
 //        }
 //    }
 //    else {
 //        return Position(0, 0);  // you might be dead at this point, fyi.
 //    }
 //}


 Unit_Inventory operator+(const Unit_Inventory& lhs, const Unit_Inventory& rhs)
 {
    Unit_Inventory total = lhs;
    total.unit_inventory_.insert(rhs.unit_inventory_.begin(), rhs.unit_inventory_.end());
    total.updateUnitInventorySummary();
    return total;
 }

 Unit_Inventory operator-(const Unit_Inventory& lhs, const Unit_Inventory& rhs)
 {
     Unit_Inventory total;
     total.unit_inventory_.insert(lhs.unit_inventory_.begin(), lhs.unit_inventory_.end());

     for (map<Unit,Stored_Unit>::const_iterator& it = rhs.unit_inventory_.begin(); it != rhs.unit_inventory_.end();) {
         if (total.unit_inventory_.find(it->first) != total.unit_inventory_.end() ) {
             total.unit_inventory_.erase(it->first);
         }
         else {
             it++;
         }
     }
     total.updateUnitInventorySummary();
     return total;
 }

void Unit_Inventory::updateUnitInventorySummary() {
    //Tally up crucial details about enemy. Should be doing this onclass. Perhaps make an enemy summary class?

    int fliers = 0;
    int ground_unit = 0;
    int shoots_up = 0;
    int shoots_down = 0;
    int shoots_both = 0;
    int high_ground = 0;
    int range = 0;
	int worker_count = 0;
	int volume = 0;
    int detector_count = 0;
    int cloaker_count = 0;
    int max_cooldown = 0;
    int ground_fodder = 0;
    int air_fodder = 0;
    int resource_depots = 0;
    int future_fap_stock = 0;
    int moving_average_fap_stock = 0;
    int stock_full_health = 0;
    int is_shooting = 0;
    vector<UnitType> already_seen_types;

    for ( auto const & u_iter : unit_inventory_ ) { // should only search through unit types not per unit.

        future_fap_stock += u_iter.second.future_fap_value_;
        moving_average_fap_stock += u_iter.second.ma_future_fap_value_;
        is_shooting += u_iter.second.cd_remaining_ > 0; //

        if ( find( already_seen_types.begin(), already_seen_types.end(), u_iter.second.type_ ) == already_seen_types.end() ) { // if you haven't already checked this unit type.
            
            bool flying_unit = u_iter.second.type_.isFlyer();
            int unit_value_for_all_of_type = CUNYAIModule::Stock_Units(u_iter.second.type_, *this);
            int count_of_unit_type = CUNYAIModule::Count_Units(u_iter.second.type_, *this) ;
            if ( CUNYAIModule::IsFightingUnit(u_iter.second) ) {

                bool up_gun = u_iter.second.type_.airWeapon() != WeaponTypes::None || u_iter.second.type_== UnitTypes::Terran_Bunker;
                bool down_gun = u_iter.second.type_.groundWeapon() != WeaponTypes::None || u_iter.second.type_ == UnitTypes::Terran_Bunker;
                bool cloaker = u_iter.second.type_.isCloakable() || u_iter.second.type_ == UnitTypes::Zerg_Lurker || u_iter.second.type_.hasPermanentCloak();
                int range_temp = (bool)(u_iter.second.bwapi_unit_) * CUNYAIModule::getProperRange(u_iter.second.type_, u_iter.second.bwapi_unit_->getPlayer()) + !(bool)(u_iter.second.bwapi_unit_) * CUNYAIModule::getProperRange(u_iter.second.type_, Broodwar->enemy());
                
                fliers          += flying_unit * unit_value_for_all_of_type; // add the value of that type of unit to the flier stock.
                ground_unit     += !flying_unit * unit_value_for_all_of_type;
                shoots_up       += up_gun * unit_value_for_all_of_type;
                shoots_down     += down_gun * unit_value_for_all_of_type;
                shoots_both     += (up_gun && down_gun) * unit_value_for_all_of_type;
                cloaker_count   += cloaker * count_of_unit_type;
                detector_count  += u_iter.second.type_.isDetector() * count_of_unit_type;
                max_cooldown = max(max(u_iter.second.type_.groundWeapon().damageCooldown(), u_iter.second.type_.airWeapon().damageCooldown()), max_cooldown);
                range = (range_temp > range) * range_temp + !(range_temp > range) * range;

                //if (u_iter.second.type_ == UnitTypes::Terran_Bunker && 7 * 32 < range) {
                //    range = 7 * 32; // depends on upgrades and unit contents.
                //}

                already_seen_types.push_back( u_iter.second.type_ );
            }
            else {
                resource_depots += u_iter.second.type_.isResourceDepot() * count_of_unit_type;
                air_fodder += flying_unit * unit_value_for_all_of_type; // add the value of that type of unit to the flier stock.
                ground_fodder += !flying_unit * unit_value_for_all_of_type;
            
            }
            stock_full_health += u_iter.second.stock_value_ * count_of_unit_type;
            volume += !flying_unit * u_iter.second.type_.height()*u_iter.second.type_.width() * count_of_unit_type;
			//Region r = Broodwar->getRegionAt( u_iter.second.pos_ );
   //         if ( r && u_iter.second.valid_pos_ && u_iter.second.type_ != UnitTypes::Buildings ) {
   //             if ( r->isHigherGround() || r->getDefensePriority() > 1 ) {
   //                 high_ground += u_iter.second.current_stock_value_;
   //             }
   //         }
        }
    } 

	worker_count = CUNYAIModule::Count_Units(UnitTypes::Zerg_Drone, *this) + CUNYAIModule::Count_Units(UnitTypes::Protoss_Probe, *this) + CUNYAIModule::Count_Units(UnitTypes::Terran_SCV, *this);

    stock_fliers_ = fliers;
    stock_ground_units_ = ground_unit;
    stock_both_up_and_down_ = shoots_both;
    stock_shoots_up_ = shoots_up;
    stock_shoots_down_ = shoots_down;
    stock_high_ground_= high_ground;
    stock_fighting_total_ = stock_ground_units_ + stock_fliers_;
    stock_ground_fodder_ = ground_fodder;
    stock_air_fodder_ = air_fodder;
    stock_total_ = stock_fighting_total_ + stock_ground_fodder_ + stock_air_fodder_;
    max_range_ = range;
    max_cooldown_ = max_cooldown;
	worker_count_ = worker_count;
	volume_ = volume;
    detector_count_ = detector_count;
    cloaker_count_ = cloaker_count;
    resource_depot_count_ = resource_depots;
    future_fap_stock_ = future_fap_stock;
    moving_average_fap_stock_ = moving_average_fap_stock;
    stock_full_health_ = stock_full_health;
    is_shooting_ = is_shooting;
}

void Unit_Inventory::stopMine(Unit u, Resource_Inventory& ri) {
    if (u->getType().isWorker()) {
        Stored_Unit& miner = unit_inventory_.find(u)->second;
        miner.stopMine(ri);
    }
}

//Stored_Unit functions.
Stored_Unit::Stored_Unit() = default;

//returns a steryotypical unit only.
Stored_Unit::Stored_Unit( const UnitType &unittype ) {
    valid_pos_ = false;
    type_ = unittype;
    build_type_ = UnitTypes::None;
    shields_ = unittype.maxShields();
    health_ = unittype.maxHitPoints();
    current_hp_ = shields_ + health_;
    locked_mine_ = nullptr;
    circumference_ = type_.height() * 2 + type_.width() * 2;
    circumference_remaining_ = circumference_;
    is_flying_ = unittype.isFlyer();
    elevation_ = 0; //inaccurate and will need to be fixed.
    cd_remaining_ = 0;
    stimmed_ = false;

    //Get unit's status. Precalculated, precached.
    int modified_supply = unittype.getRace() == Races::Zerg && unittype.isBuilding() ? unittype.supplyRequired() + 2 :unittype.supplyRequired(); // Zerg units cost a supply (2, technically since BW cuts it in half.)
    modified_supply = unittype == UnitTypes::Terran_Bunker ? unittype.supplyRequired() + 2 :unittype.supplyRequired(); // Assume bunkers are loaded.
    int modified_min_cost = unittype == UnitTypes::Terran_Bunker ? unittype.mineralPrice() + 50 :unittype.mineralPrice(); // Assume bunkers are loaded.
    int modified_gas_cost = unittype.gasPrice();

    stock_value_ = modified_min_cost + 1.25 * modified_gas_cost + 25 * modified_supply;

    stock_value_ /= (1 + (int)unittype.isTwoUnitsInOneEgg()); // condensed /2 into one line to avoid if-branch prediction.

    current_stock_value_ = stock_value_; // Precalculated, precached.
};

// We must be able to create Stored_Unit objects as well.
Stored_Unit::Stored_Unit( const Unit &unit ) {
    valid_pos_ = true;
    unit_ID_ = unit->getID();
    bwapi_unit_ = unit;
    pos_ = unit->getPosition();
    type_ = unit->getType();
    build_type_ = unit->getBuildType();
    shields_ = unit->getShields();
    health_ = unit->getHitPoints();
    current_hp_ = shields_ + health_;
	locked_mine_ = nullptr;
    velocity_x_ = unit->getVelocityX();
    velocity_y_ = unit->getVelocityY();
    order_ = unit->getOrder();
    command_ = unit->getLastCommand();
    time_since_last_command_ = Broodwar->getFrameCount() - unit->getLastCommandFrame();
    circumference_ = type_.height() * 2 + type_.width() * 2;
    circumference_remaining_ = circumference_;

    //Needed for FAP.
        is_flying_ = unit->isFlying();
        elevation_ = BWAPI::Broodwar->getGroundHeight(TilePosition(pos_));
        cd_remaining_ = unit->getAirWeaponCooldown();
        stimmed_ = unit->isStimmed();

    //Get unit's status. Precalculated, precached.
    stock_value_ = Stored_Unit(type_).stock_value_; //prevents retyping.
    ma_future_fap_value_ = stock_value_;
    future_fap_value_ = stock_value_;
    current_stock_value_ = (int)(stock_value_ * current_hp_ / (double)( type_.maxHitPoints() + type_.maxShields() ) ); // Precalculated, precached.
}


//Increments the number of miners on a resource.
void Stored_Unit::startMine(Stored_Resource &new_resource, Resource_Inventory &ri){
	locked_mine_ = new_resource.bwapi_unit_;
	ri.resource_inventory_.find(locked_mine_)->second.number_of_miners_++;
}

//Decrements the number of miners on a resource.
void Stored_Unit::stopMine(Resource_Inventory &ri){
	if (locked_mine_){
        if (getMine(ri)) {
            getMine(ri)->number_of_miners_ = max(getMine(ri)->number_of_miners_ - 1, 0);
        }
	}
    locked_mine_ = nullptr;
}

//finds mine- Will return true something even if the mine DNE.
Stored_Resource* Stored_Unit::getMine(Resource_Inventory &ri) {
    Stored_Resource* tenative_resource = nullptr;
    if (ri.resource_inventory_.find(locked_mine_) != ri.resource_inventory_.end()) {
        tenative_resource = &ri.resource_inventory_.find(locked_mine_)->second;
    }
    return tenative_resource;
}

//checks if mine started with less than 8 resource
bool Stored_Unit::isAssignedClearing( Resource_Inventory &ri ) {
    if ( locked_mine_ ) {
        if (Stored_Resource* mine_of_choice = this->getMine(ri)) { // if it has an associated mine.
            return mine_of_choice->max_stock_value_ <= 8;
        }
    }
    return false;
}

//checks if mine started with less than 8 resource
bool Stored_Unit::isAssignedLongDistanceMining(Resource_Inventory &ri) {
    if (locked_mine_) {
        if (Stored_Resource* mine_of_choice = this->getMine(ri)) { // if it has an associated mine.
            return mine_of_choice->max_stock_value_ >= 8 && !mine_of_choice->local_natural_;
        }
    }
    return false;
}

//checks if worker is assigned to a mine that started with more than 8 resources (it is a proper mine).
bool Stored_Unit::isAssignedMining(Resource_Inventory &ri) {
    if (locked_mine_) {
        if (ri.resource_inventory_.find(locked_mine_) != ri.resource_inventory_.end()) {
            Stored_Resource* mine_of_choice = this->getMine(ri);
            return mine_of_choice->max_stock_value_ >= 8 && mine_of_choice->type_.isMineralField();
        }
    }
    return false;
}

bool Stored_Unit::isAssignedGas(Resource_Inventory &ri) {
    if (locked_mine_) {
        if (ri.resource_inventory_.find(locked_mine_) != ri.resource_inventory_.end()) {
            Stored_Resource* mine_of_choice = this->getMine(ri);
            return mine_of_choice->type_.isRefinery();
        }
    }
    return false;
}

bool Stored_Unit::isAssignedResource(Resource_Inventory  &ri) {

    return Stored_Unit::isAssignedMining(ri) || Stored_Unit::isAssignedGas(ri);

}

// Warning- depends on unit being updated.
bool Stored_Unit::isAssignedBuilding(Resource_Inventory  &ri) {
    this->updateStoredUnit(this->bwapi_unit_); // unit needs to be updated to confirm this.
    bool building_sent = (build_type_.isBuilding() || order_ == Orders::Move || order_ == Orders::ZergBuildingMorph || command_.getType() == UnitCommandTypes::Build || command_.getType() == UnitCommandTypes::Morph ) && time_since_last_command_ < 15 * 24 && !isAssignedResource(ri);

    return building_sent;
}

//if the miner is not doing any thing
bool Stored_Unit::isNoLock(){
    this->updateStoredUnit(this->bwapi_unit_); // unit needs to be updated to confirm this.
    return  bwapi_unit_ && !bwapi_unit_->getOrderTarget();
}

//if the miner is not mining his target. Target must be visible.
bool Stored_Unit::isBrokenLock(Resource_Inventory &ri) {
    this->updateStoredUnit(this->bwapi_unit_); // unit needs to be updated to confirm this.
    Stored_Resource* target_mine = this->getMine(ri); // target mine must be visible to be broken. Otherwise it is a long range lock.
    return !(isLongRangeLock(ri) || isLocallyLocked(ri)); // Or its order target is not the mine, then we have a broken lock.
}

bool Stored_Unit::isLocallyLocked(Resource_Inventory &ri) {
    this->updateStoredUnit(this->bwapi_unit_); // unit needs to be updated to confirm this.
    return  locked_mine_ && bwapi_unit_->getOrderTarget() && bwapi_unit_->getOrderTarget() == locked_mine_; // Everything must be visible and properly assigned.
}

//prototypeing
bool Stored_Unit::isLongRangeLock(Resource_Inventory &ri) {
    this->updateStoredUnit(this->bwapi_unit_); // unit needs to be updated to confirm this.
    Stored_Resource* target_mine = this->getMine(ri);
    return bwapi_unit_ && target_mine && target_mine->pos_ && (!Broodwar->isVisible(TilePosition(target_mine->pos_)) /*|| (target_mine->bwapi_unit_ && target_mine->bwapi_unit_->isMorphing())*/);
}

bool Stored_Unit::isMovingLock(Resource_Inventory &ri) {
    this->updateStoredUnit(this->bwapi_unit_); // unit needs to be updated to confirm this.
    Stored_Resource* target_mine = this->getMine(ri);
    bool contents_of_long_range_lock_without_visiblity = bwapi_unit_ && target_mine && target_mine->pos_;
    return  contents_of_long_range_lock_without_visiblity && Broodwar->isVisible(TilePosition(target_mine->pos_));
}

auto Stored_Unit::convertToFAP() {
    return FAP::makeUnit<Stored_Unit*>()
        .setData(this)
        .setUnitType(type_)
        .setPosition(pos_)
        .setHealth(health_)
        .setShields(shields_)
        .setFlying(is_flying_)
        .setElevation(elevation_)
        .setAttackerCount(2)
        .setArmorUpgrades(0) // ignored for now
        .setAttackUpgrades(0) // ignored for now
        .setShieldUpgrades(0) // ignored for now
        .setSpeedUpgrade(false) // ignored for now
        .setAttackSpeedUpgrade(false) // ignored for now
        .setAttackCooldownRemaining(cd_remaining_)
        .setStimmed(stimmed_)
        .setRangeUpgrade(false) // ignored for now
        ;
}

auto Stored_Unit::convertToFAPPosition(const Position &chosen_pos) {
    return FAP::makeUnit<Stored_Unit*>() // don't care about type.
        .setData(this)
        .setUnitType(type_)
        .setPosition(chosen_pos)
        .setHealth(health_)
        .setShields(shields_)
        .setFlying(is_flying_)
        .setElevation(elevation_)
        .setAttackerCount(2)
        .setArmorUpgrades(0) // ignored for now
        .setAttackUpgrades(0) // ignored for now
        .setShieldUpgrades(0) // ignored for now
        .setSpeedUpgrade(false) // ignored for now
        .setAttackSpeedUpgrade(false) // ignored for now
        .setAttackCooldownRemaining(cd_remaining_)
        .setStimmed(stimmed_)
        .setRangeUpgrade(false) // ignored for now
        ;
}

void Stored_Unit::updateFAPvalue(FAP::FAPUnit<Stored_Unit*> &fap_unit)
{
    fap_unit.data->future_fap_value_ = (int)(fap_unit.data->stock_value_ * (fap_unit.health + fap_unit.shields) / (double)(fap_unit.maxHealth + fap_unit.maxShields));
    fap_unit.data->updated_fap_this_frame_ = true;
}

void Stored_Unit::updateFAPvalueDead()
{
    future_fap_value_ = 0;
    updated_fap_this_frame_ = true;
}

bool Stored_Unit::unitAliveinFuture(const Stored_Unit &unit, const int &number_of_frames_in_future) {
    return unit.ma_future_fap_value_ <= unit.stock_value_ * (_MOVING_AVERAGE_DURATION - number_of_frames_in_future) / (double)_MOVING_AVERAGE_DURATION;
}

void Unit_Inventory::addToFriendlyFAP(FAP::FastAPproximation<Stored_Unit*> &fap_object) {
    for (auto &u : unit_inventory_) {
        fap_object.addUnitPlayer1(u.second.convertToFAP());
    }
}

void Unit_Inventory::addToEnemyFAP(FAP::FastAPproximation<Stored_Unit*> &fap_object) {
    for (auto &u : unit_inventory_) {
        fap_object.addUnitPlayer2(u.second.convertToFAP());
    }
}

void Unit_Inventory::addToFriendlyBuildFAP( FAP::FastAPproximation<Stored_Unit*> &fap_object) {
    std::default_random_engine generator;  //Will be used to obtain a seed for the random number engine
    std::uniform_int_distribution<int> dis(0, CUNYAIModule::inventory.my_portion_of_the_map_/2);    // default values for output.
    for (auto &u : unit_inventory_) {
        int rand_x = dis(generator);
        int rand_y = dis(generator);
        if(CUNYAIModule::IsFightingUnit(u.second)) fap_object.addUnitPlayer1(u.second.convertToFAPPosition(Position(rand_x, rand_y)));
    }
}

void Unit_Inventory::addToEnemyBuildFAP(FAP::FastAPproximation<Stored_Unit*> &fap_object) {
    std::default_random_engine generator;  //Will be used to obtain a seed for the random number engine
    std::uniform_int_distribution<int> dis(CUNYAIModule::inventory.my_portion_of_the_map_/2, CUNYAIModule::inventory.my_portion_of_the_map_);    // default values for output.
    for (auto &u : unit_inventory_) {
        int rand_x = dis(generator);
        int rand_y = dis(generator);
        if (CUNYAIModule::IsFightingUnit(u.second)) fap_object.addUnitPlayer2(u.second.convertToFAPPosition(Position(rand_x, rand_y)));
    }
}

//This call seems very inelgant. Check if it can be made better.
void Unit_Inventory::pullFromFAP(vector<FAP::FAPUnit<Stored_Unit*>> &fap_vector)
{
    for (auto &u : unit_inventory_) {
        u.second.updated_fap_this_frame_ = false;
    }

    for (auto &fu : fap_vector) {
        if ( fu.data ) {
            Stored_Unit::updateFAPvalue(fu);
        }
    }

    for (auto &u : unit_inventory_) {
        if (!u.second.updated_fap_this_frame_) { u.second.updateFAPvalueDead(); }
    }

}

Stored_Unit* Unit_Inventory::getStoredUnit(const Unit & unit)
{
    auto& find_result = unit_inventory_.find(unit);
    if (find_result != unit_inventory_.end()) return &find_result->second;
    else return nullptr;
}
Stored_Unit Unit_Inventory::getStoredUnitValue(const Unit & unit) const
{
    auto& find_result = unit_inventory_.find(unit);
    if (find_result != unit_inventory_.end()) return find_result->second;
    else return nullptr;
}


void CUNYAIModule::printUnitInventory(Unit_Inventory inventory)
{
    ofstream output; // Prints to brood war file while in the WRITE file.
    if (Broodwar->getFrameCount() % (24 * 30) == 0) {
        output.open(".\\bwapi-data\\write\\" + Broodwar->mapFileName() + "_status.txt", ios_base::app);
        output << "Friendly " << inventory.cloaker_count_ << "," << inventory.detector_count_ << ',' << inventory.max_cooldown_ << ',' << inventory.max_range_ << ',' << inventory.resource_depot_count_ << ',' << inventory.stock_air_fodder_ << ',' << inventory.stock_both_up_and_down_ << ',' << inventory.stock_fighting_total_ << ',' << inventory.stock_fliers_ << ',' << inventory.stock_ground_fodder_ << ',' << inventory.stock_ground_units_ << ',' << inventory.stock_high_ground_ << ',' << inventory.stock_shoots_down_ << ',' << inventory.stock_shoots_up_ << ',' << inventory.stock_total_ << endl;
        output.close();
    }
}