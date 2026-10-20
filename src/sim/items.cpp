// items.cpp — item and fluid names and item categories (see game_types.h).
#include "sim/game_types.h"

const char* item_name(ItemKind kind) {
    switch (kind) {
    case ItemKind::Hull:              return "Hull";
    case ItemKind::ReinforcedHull:    return "Reinforced hull";
    case ItemKind::TransportBelt:     return "Transport belt";
    case ItemKind::ExpressBelt:       return "Express belt";
    case ItemKind::BeltTunnel:        return "Belt tunnel";
    case ItemKind::Splitter:          return "Splitter";
    case ItemKind::FilterSorter:      return "Filter sorter";
    case ItemKind::CargoHold:         return "Cargo hold";
    case ItemKind::Pipe:              return "Pipe";
    case ItemKind::Pump:              return "Pump";
    case ItemKind::FluidTank:         return "Fluid tank";
    case ItemKind::AsteroidCollector: return "Asteroid collector";
    case ItemKind::TractorArray:      return "Tractor array";
    case ItemKind::HydrogenCollector: return "Hydrogen collector";
    case ItemKind::AsteroidCrusher:   return "Asteroid crusher";
    case ItemKind::SolarFurnace:      return "Solar furnace";
    case ItemKind::ArcFurnace:        return "Arc furnace";
    case ItemKind::IceMelter:         return "Ice melter";
    case ItemKind::Electrolyzer:      return "Electrolyzer";
    case ItemKind::ChemicalPlant:     return "Chemical plant";
    case ItemKind::FabricatorMk1:     return "Fabricator Mk1";
    case ItemKind::FabricatorMk2:     return "Fabricator Mk2";
    case ItemKind::FabricatorMk3:     return "Fabricator Mk3";
    case ItemKind::SolarPanel:        return "Solar panel";
    case ItemKind::SolarArray:        return "Solar array";
    case ItemKind::FuelCellGenerator: return "Fuel-cell generator";
    case ItemKind::BatteryBank:       return "Battery bank";
    case ItemKind::SurveyScanner:     return "Survey scanner";
    case ItemKind::IonThruster:       return "Ion thruster";
    case ItemKind::MetallicChunk:     return "Metallic chunk";
    case ItemKind::SilicateChunk:     return "Silicate chunk";
    case ItemKind::CarbonaceousChunk: return "Carbonaceous chunk";
    case ItemKind::IceChunk:          return "Ice chunk";
    case ItemKind::ExoticChunk:       return "Exotic chunk";
    case ItemKind::IronOre:           return "Iron ore";
    case ItemKind::NickelOre:         return "Nickel ore";
    case ItemKind::CopperOre:         return "Copper ore";
    case ItemKind::Silica:            return "Silica";
    case ItemKind::Carbon:            return "Carbon";
    case ItemKind::WaterIce:          return "Water ice";
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

const char* fluid_name(FluidKind kind) {
    switch (kind) {
    case FluidKind::Water:    return "Water";
    case FluidKind::Hydrogen: return "Hydrogen";
    case FluidKind::Oxygen:   return "Oxygen";
    case FluidKind::Count:    break;
    }
    return "?";
}
