#pragma once

#include <BWAPI.h>
#include "Source\CUNYAIModule.h"
#include "Source\InventoryManager.h"
#include "Source\Unit_Inventory.h"
#include "Source\Resource_Inventory.h"
#include "Source\Reservation_Manager.h"
#include <algorithm>

using namespace std;
using namespace BWAPI;

Reservation::Reservation() {
    min_reserve_ = 0;
    gas_reserve_ = 0;
    building_timer_ = 0;
    last_builder_sent_ = 0;
}

bool Reservation::addReserveSystem( UnitType type, TilePosition pos ) {
    bool safe = reservation_map_.insert({ type, pos }).second;
    if ( safe ) {
        min_reserve_ += type.mineralPrice();
        gas_reserve_ += type.gasPrice();
        building_timer_ = type.buildTime() > building_timer_ ? type.buildTime() : building_timer_;
        last_builder_sent_ = Broodwar->getFrameCount();
    }

    return safe;
}

void Reservation::removeReserveSystem(UnitType type) {
    map<UnitType, TilePosition>::iterator it = reservation_map_.find(type);
    if (it != reservation_map_.end()) {
        reservation_map_.erase(type);
        min_reserve_ -= type.mineralPrice();
        gas_reserve_ -= type.gasPrice();
    }
    else if (type != UnitTypes::None && _ANALYSIS_MODE) {
        Broodwar->sendText("We're trying to remove %s from the reservation queue but can't find it.", type.c_str());
    }
};

void Reservation::decrementReserveTimer() {
    if ( Broodwar->getFrameCount() == 0 ) {
        building_timer_ = 0;
    }
    else {
        building_timer_ > 0 ? --building_timer_ : 0;
    }
}

int Reservation::getExcessMineral() {
    return max( Broodwar->self()->minerals() - min_reserve_ , 0 ) ;
}

int Reservation::getExcessGas() {
    return max( Broodwar->self()->gas() - gas_reserve_ , 0 );
}

bool Reservation::checkExcessIsGreaterThan(const UnitType &type) const {
    bool okay_on_gas = Broodwar->self()->gas() - gas_reserve_ > type.gasPrice() || type.gasPrice() == 0;
    bool okay_on_minerals = Broodwar->self()->minerals() > type.mineralPrice() || type.mineralPrice() == 0;
    return okay_on_gas && okay_on_minerals;
}

bool Reservation::checkExcessIsGreaterThan(const TechType &type) const {
    return Broodwar->self()->gas() - gas_reserve_ > type.gasPrice() && Broodwar->self()->minerals() > type.mineralPrice();
}

bool Reservation::checkAffordablePurchase( const UnitType type ) { // make a template?
    bool affordable = Broodwar->self()->minerals() - min_reserve_ >= type.mineralPrice() && Broodwar->self()->gas() - gas_reserve_ >= type.gasPrice();
    bool open_reservation = reservation_map_.empty() || reservation_map_.find(type)==reservation_map_.end();
    return affordable && open_reservation;
}

int Reservation::countTimesWeCanAffordPurchase(const UnitType type) {
    bool affordable = true;
    int i = 0;
    bool open_reservation = reservation_map_.empty() || reservation_map_.find(type) == reservation_map_.end();

    while (affordable) {
        affordable = Broodwar->self()->minerals() - i * type.mineralPrice() >= min_reserve_ && Broodwar->self()->gas() - i * type.gasPrice() >= gas_reserve_;
        if(affordable) i++;
    }
    return affordable && open_reservation;
}

bool Reservation::checkAffordablePurchase( const TechType type ) {
    return Broodwar->self()->minerals() - min_reserve_ >= type.mineralPrice() && Broodwar->self()->gas() - gas_reserve_ >= type.gasPrice();
}

bool Reservation::checkAffordablePurchase( const UpgradeType type ) {
    return Broodwar->self()->minerals() - min_reserve_ >= type.mineralPrice() && Broodwar->self()->gas() - gas_reserve_ >= type.gasPrice();
}

void Reservation::confirmOngoingReservations( const Unit_Inventory &ui) {

    min_reserve_ = 0;
    gas_reserve_ = 0;

    for (auto res_it = reservation_map_.begin(); res_it != reservation_map_.end() && !reservation_map_.empty(); ) {
        bool keep = false;

        for ( auto unit_it = ui.unit_inventory_.begin(); unit_it != ui.unit_inventory_.end() && !ui.unit_inventory_.empty(); unit_it++ ) {
            if ( res_it->second == unit_it->second.bwapi_unit_->getLastCommand().getTargetTilePosition() ) {
                keep = true;
            }
        } // check if we have a unit building it.

        if ( keep ) {
            ++res_it;
        }
        else {
            Broodwar->sendText( "No evidience a worker is building the reserved %s. Freeing up the funds.", res_it->first.c_str() );
            UnitType remove_me = res_it->first;
            res_it++;
            removeReserveSystem( remove_me );  // contains an erase.
        }
    }

    for (auto res_it = reservation_map_.begin(); res_it != reservation_map_.end() && !reservation_map_.empty(); res_it++ ) {
        min_reserve_ += res_it->first.mineralPrice();
        gas_reserve_ += res_it->first.gasPrice();
    }

    if ( !reservation_map_.empty() && last_builder_sent_ < Broodwar->getFrameCount() - 30 * 24) {
        Broodwar->sendText( "...We're stuck, aren't we? Have a friendly nudge." );
        reservation_map_.clear();
        min_reserve_ = 0;
        gas_reserve_ = 0;
    }
}