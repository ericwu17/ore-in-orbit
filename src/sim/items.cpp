// items.cpp — item names and ids, and item categories (see
// game_types.h).
#include "sim/game_types.h"

const char* item_name(ItemKind kind) {
    switch (kind) {
    case ItemKind::Hull:              return "Hull";
    case ItemKind::ReinforcedHull:    return "Reinforced hull";
    case ItemKind::TransportBelt:     return "Transport belt";
    case ItemKind::ExpressBelt:       return "Express belt";
    case ItemKind::BeltTunnel:        return "Belt tunnel";
    case ItemKind::FilterSorter:      return "Filter sorter";
    case ItemKind::ReverseSorter:     return "Reverse sorter";
    case ItemKind::Junction:          return "Junction";
    case ItemKind::Router:            return "Router";
    case ItemKind::CargoHold:         return "Cargo hold";
    case ItemKind::AsteroidCollector: return "Asteroid collector";
    case ItemKind::TractorArray:      return "Tractor array";
    case ItemKind::AsteroidCrusher:   return "Asteroid crusher";
    case ItemKind::SolarFurnace:      return "Solar furnace";
    case ItemKind::ArcFurnace:        return "Arc furnace";
    case ItemKind::ChemicalPlant:     return "Chemical plant";
    case ItemKind::FabricatorMk1:     return "Fabricator Mk1";
    case ItemKind::FabricatorMk2:     return "Fabricator Mk2";
    case ItemKind::FabricatorMk3:     return "Fabricator Mk3";
    case ItemKind::SolarPanel:        return "Solar panel";
    case ItemKind::SolarArray:        return "Solar array";
    case ItemKind::BatteryBank:       return "Battery bank";
    case ItemKind::SurveyScanner:     return "Survey scanner";
    case ItemKind::IonThruster:       return "Ion thruster";
    case ItemKind::MetallicChunk:     return "Metallic chunk";
    case ItemKind::SilicateChunk:     return "Silicate chunk";
    case ItemKind::CarbonaceousChunk: return "Carbonaceous chunk";
    case ItemKind::ExoticChunk:       return "Exotic chunk";
    case ItemKind::IronOre:           return "Iron ore";
    case ItemKind::NickelOre:         return "Nickel ore";
    case ItemKind::CopperOre:         return "Copper ore";
    case ItemKind::Silica:            return "Silica";
    case ItemKind::Carbon:            return "Carbon";
    case ItemKind::IridiumOre:        return "Iridium ore";
    case ItemKind::IronPlate:         return "Iron plate";
    case ItemKind::CopperPlate:       return "Copper plate";
    case ItemKind::NickelPlate:       return "Nickel plate";
    case ItemKind::Glass:             return "Glass";
    case ItemKind::Steel:             return "Steel";
    case ItemKind::Silicon:           return "Silicon";
    case ItemKind::IridiumPlate:      return "Iridium plate";
    case ItemKind::Plastic:           return "Plastic";
    case ItemKind::Battery:           return "Battery";
    case ItemKind::PropellantCell:    return "Propellant cell";
    case ItemKind::Gear:              return "Gear";
    case ItemKind::CopperWire:        return "Copper wire";
    case ItemKind::Circuit:           return "Circuit";
    case ItemKind::ServoMotor:        return "Servo motor";
    case ItemKind::StructuralFrame:   return "Structural frame";
    case ItemKind::PhotovoltaicCell:  return "Photovoltaic cell";
    case ItemKind::AdvancedCircuit:   return "Advanced circuit";
    case ItemKind::Processor:         return "Processor";
    case ItemKind::Count:             break;
    }
    return "?";
}

// No default case, so a new ItemKind without an id is a -Wswitch error.
const char* item_id(ItemKind kind) {
    switch (kind) {
    case ItemKind::Hull:              return "Hull";
    case ItemKind::ReinforcedHull:    return "ReinforcedHull";
    case ItemKind::TransportBelt:     return "TransportBelt";
    case ItemKind::ExpressBelt:       return "ExpressBelt";
    case ItemKind::BeltTunnel:        return "BeltTunnel";
    case ItemKind::FilterSorter:      return "FilterSorter";
    case ItemKind::ReverseSorter:     return "ReverseSorter";
    case ItemKind::Junction:          return "Junction";
    case ItemKind::Router:            return "Router";
    case ItemKind::CargoHold:         return "CargoHold";
    case ItemKind::AsteroidCollector: return "AsteroidCollector";
    case ItemKind::TractorArray:      return "TractorArray";
    case ItemKind::AsteroidCrusher:   return "AsteroidCrusher";
    case ItemKind::SolarFurnace:      return "SolarFurnace";
    case ItemKind::ArcFurnace:        return "ArcFurnace";
    case ItemKind::ChemicalPlant:     return "ChemicalPlant";
    case ItemKind::FabricatorMk1:     return "FabricatorMk1";
    case ItemKind::FabricatorMk2:     return "FabricatorMk2";
    case ItemKind::FabricatorMk3:     return "FabricatorMk3";
    case ItemKind::SolarPanel:        return "SolarPanel";
    case ItemKind::SolarArray:        return "SolarArray";
    case ItemKind::BatteryBank:       return "BatteryBank";
    case ItemKind::SurveyScanner:     return "SurveyScanner";
    case ItemKind::IonThruster:       return "IonThruster";
    case ItemKind::MetallicChunk:     return "MetallicChunk";
    case ItemKind::SilicateChunk:     return "SilicateChunk";
    case ItemKind::CarbonaceousChunk: return "CarbonaceousChunk";
    case ItemKind::ExoticChunk:       return "ExoticChunk";
    case ItemKind::IronOre:           return "IronOre";
    case ItemKind::NickelOre:         return "NickelOre";
    case ItemKind::CopperOre:         return "CopperOre";
    case ItemKind::Silica:            return "Silica";
    case ItemKind::Carbon:            return "Carbon";
    case ItemKind::IridiumOre:        return "IridiumOre";
    case ItemKind::IronPlate:         return "IronPlate";
    case ItemKind::CopperPlate:       return "CopperPlate";
    case ItemKind::NickelPlate:       return "NickelPlate";
    case ItemKind::Glass:             return "Glass";
    case ItemKind::Steel:             return "Steel";
    case ItemKind::Silicon:           return "Silicon";
    case ItemKind::IridiumPlate:      return "IridiumPlate";
    case ItemKind::Plastic:           return "Plastic";
    case ItemKind::Battery:           return "Battery";
    case ItemKind::PropellantCell:    return "PropellantCell";
    case ItemKind::Gear:              return "Gear";
    case ItemKind::CopperWire:        return "CopperWire";
    case ItemKind::Circuit:           return "Circuit";
    case ItemKind::ServoMotor:        return "ServoMotor";
    case ItemKind::StructuralFrame:   return "StructuralFrame";
    case ItemKind::PhotovoltaicCell:  return "PhotovoltaicCell";
    case ItemKind::AdvancedCircuit:   return "AdvancedCircuit";
    case ItemKind::Processor:         return "Processor";
    case ItemKind::Count:             break;
    }
    return "?";
}

ItemCategory item_category(ItemKind kind) {
    const int k = static_cast<int>(kind);
    if (is_placeable(kind))
        return ItemCategory::Placeable;
    if (k < static_cast<int>(ItemKind::IronOre))
        return ItemCategory::AsteroidChunk;
    if (k < static_cast<int>(ItemKind::IronPlate))
        return ItemCategory::RawResource;
    if (k < static_cast<int>(ItemKind::Gear))
        return ItemCategory::Material;
    return ItemCategory::Component;
}
