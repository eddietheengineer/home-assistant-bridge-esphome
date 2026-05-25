/*!
 * @file
 * @brief Stub implementations for functions that would normally be provided by ESPHome.
 */

#include <string>
#include <cstdint>

std::string appliance_type_to_string(uint8_t appliance_type)
{
  // Auto-generated from public-appliance-api-documentation
  // ERD 0x0008 - Appliance Type enum mapping (sanitized for C++)
  switch (appliance_type) {
    case 0: return "WaterHeater";
    case 1: return "ClothesDryer";
    case 2: return "ClothesWasher";
    case 3: return "Refrigerator";
    case 4: return "Microwave";
    case 5: return "Advantium";
    case 6: return "Dishwasher";
    case 7: return "Oven";
    case 8: return "ElectricRange";
    case 9: return "GasRange";
    case 10: return "ThermostatRAC";
    case 11: return "ElectricCooktop";
    case 12: return "PizzaOven";
    case 13: return "GasCooktop";
    case 14: return "SplitDFSDuctFreeSplitAC";
    case 15: return "Hood";
    case 16: return "PointOfEntryWaterFilter";
    case 17: return "InductionCooktop";
    case 18: return "DeliveryBox";
    case 19: return "KitchenHubVentHood";
    case 20: return "ZonelinePTAC";
    case 21: return "WaterSoftener";
    case 22: return "PortableAC";
    case 23: return "CombinationWasherDryer";
    case 24: return "DualZoneWineChiller";
    case 25: return "BeverageCenter";
    case 26: return "CoffeeBrewer";
    case 27: return "OpalNuggetIceMaker";
    case 28: return "InHomeGrower";
    case 29: return "Dehumidifer";
    case 30: return "UnderCounterIceMaker";
    case 31: return "ThroughWallAC";
    case 32: return "FPDishDrawer";
    case 33: return "EspressoCoffeeMaker";
    case 34: return "ToasterOven";
    case 35: return "ZonelineVertical";
    case 36: return "CentralDFSDuctFreeSplitController";
    case 37: return "BLEMeshGateway";
    case 38: return "StandMixer";
    case 39: return "FPCooktop";
    case 40: return "FPCooktopTeppanyaki";
    case 41: return "FPVentilationDowndraft";
    case 42: return "SmartPlug";
    case 43: return "Smoker";
    case 44: return "AirHandlerVRF";
    case 45: return "FabricCareCabinetCloset";
    case 46: return "LaundryCenter";
    case 47: return "Grill";
    case 48: return "Freezer";
    case 49: return "WarmingDrawer";
    case 50: return "VacuumSealDrawer";
    case 51: return "WineCabinet";
    case 52: return "CentralAC";
    case 53: return "SoftStarter";
    case 54: return "HearthPizzaOven";
    case 55: return "SourdoughStarter";
    case 56: return "Thermostat";
    default: return "Unknown";
  }
}
