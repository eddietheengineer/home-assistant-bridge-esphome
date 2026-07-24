#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* Auto-generated: user-selectable ERD names and value decoders. */
/* Generated from ha_discovery JSONL files - domains: switch, number, select */

#ifdef __cplusplus
extern "C" {
#endif

#define ERD_USER_SELECTABLE_COUNT 622

static const uint16_t erd_user_selectable_ids[] = {
  0x0004,
  0x0005,
  0x0006,
  0x0007,
  0x0009,
  0x000a,
  0x0032,
  0x0036,
  0x004f,
  0x0050,
  0x0105,
  0x0300,
  0x0410,
  0x0412,
  0x0416,
  0x0501,
  0x0502,
  0x0601,
  0x0801,
  0x0900,
  0x0902,
  0x1005,
  0x100a,
  0x100c,
  0x100d,
  0x100e,
  0x100f,
  0x1011,
  0x1012,
  0x1013,
  0x1017,
  0x1018,
  0x101a,
  0x101e,
  0x101f,
  0x1020,
  0x1022,
  0x1024,
  0x1028,
  0x1029,
  0x102c,
  0x102d,
  0x102f,
  0x1031,
  0x1032,
  0x1033,
  0x1034,
  0x1035,
  0x1036,
  0x1037,
  0x103b,
  0x103c,
  0x103d,
  0x103e,
  0x103f,
  0x1040,
  0x1041,
  0x1042,
  0x1043,
  0x1044,
  0x1048,
  0x1049,
  0x104a,
  0x104b,
  0x1053,
  0x1059,
  0x1062,
  0x1065,
  0x1068,
  0x1076,
  0x1165,
  0x1166,
  0x1167,
  0x1168,
  0x116a,
  0x116e,
  0x116f,
  0x1170,
  0x1171,
  0x1173,
  0x120c,
  0x121a,
  0x121e,
  0x1222,
  0x1224,
  0x1227,
  0x1229,
  0x1255,
  0x1258,
  0x125f,
  0x1263,
  0x1269,
  0x129e,
  0x12ba,
  0x12ec,
  0x1306,
  0x1308,
  0x130a,
  0x130e,
  0x130f,
  0x1311,
  0x1312,
  0x1313,
  0x1314,
  0x1315,
  0x131f,
  0x1320,
  0x1321,
  0x1322,
  0x141f,
  0x142c,
  0x14be,
  0x14c1,
  0x14c3,
  0x14c6,
  0x14c8,
  0x2015,
  0x2016,
  0x2017,
  0x2018,
  0x201b,
  0x2020,
  0x2022,
  0x2023,
  0x2031,
  0x2038,
  0x2040,
  0x2041,
  0x2044,
  0x2048,
  0x204b,
  0x204d,
  0x204e,
  0x2050,
  0x2051,
  0x2054,
  0x2057,
  0x205a,
  0x205d,
  0x2060,
  0x2063,
  0x2067,
  0x2069,
  0x206a,
  0x206d,
  0x2071,
  0x2074,
  0x2078,
  0x207b,
  0x207e,
  0x2081,
  0x2084,
  0x2087,
  0x208a,
  0x208d,
  0x2090,
  0x2093,
  0x2096,
  0x2097,
  0x209b,
  0x209d,
  0x209e,
  0x20a0,
  0x20a3,
  0x20a7,
  0x20a9,
  0x20ac,
  0x20ae,
  0x20b0,
  0x20b2,
  0x20b4,
  0x20b6,
  0x20b8,
  0x20ba,
  0x20bc,
  0x20bd,
  0x20bf,
  0x20c1,
  0x20ca,
  0x20cd,
  0x20d0,
  0x20d1,
  0x20d5,
  0x20f2,
  0x2104,
  0x2116,
  0x211d,
  0x2120,
  0x2125,
  0x2126,
  0x2129,
  0x212a,
  0x2132,
  0x2133,
  0x2137,
  0x213b,
  0x213f,
  0x2143,
  0x2149,
  0x214c,
  0x2156,
  0x215c,
  0x2164,
  0x2166,
  0x2168,
  0x216f,
  0x2171,
  0x2172,
  0x217d,
  0x2180,
  0x2206,
  0x2208,
  0x2209,
  0x222d,
  0x222f,
  0x2231,
  0x2237,
  0x223a,
  0x223c,
  0x223f,
  0x2242,
  0x2245,
  0x2248,
  0x224a,
  0x224c,
  0x224f,
  0x2252,
  0x2255,
  0x2316,
  0x2338,
  0x2f00,
  0x2f02,
  0x2f03,
  0x2f04,
  0x2f05,
  0x2f07,
  0x2f0b,
  0x2f0c,
  0x2f1b,
  0x301f,
  0x3101,
  0x3108,
  0x3109,
  0x310c,
  0x3204,
  0x320b,
  0x320d,
  0x321a,
  0x321b,
  0x321c,
  0x321d,
  0x321e,
  0x321f,
  0x3220,
  0x3221,
  0x322b,
  0x322c,
  0x322e,
  0x3232,
  0x3260,
  0x3263,
  0x3604,
  0x361a,
  0x361b,
  0x361c,
  0x361d,
  0x4008,
  0x4020,
  0x4023,
  0x4024,
  0x4025,
  0x4028,
  0x402a,
  0x4047,
  0x4048,
  0x411d,
  0x4221,
  0x4223,
  0x4226,
  0x5000,
  0x5001,
  0x5003,
  0x5009,
  0x5012,
  0x5014,
  0x5016,
  0x5019,
  0x5023,
  0x5029,
  0x502b,
  0x502c,
  0x502d,
  0x502f,
  0x5031,
  0x504e,
  0x5053,
  0x5061,
  0x5071,
  0x5100,
  0x5105,
  0x5106,
  0x510f,
  0x5111,
  0x511a,
  0x511c,
  0x5121,
  0x5124,
  0x5125,
  0x5126,
  0x5127,
  0x5128,
  0x5151,
  0x5200,
  0x5205,
  0x5206,
  0x520f,
  0x5211,
  0x521a,
  0x521c,
  0x5221,
  0x5224,
  0x5225,
  0x5226,
  0x5227,
  0x5228,
  0x5251,
  0x5402,
  0x5404,
  0x5406,
  0x540d,
  0x540e,
  0x5413,
  0x5416,
  0x5423,
  0x5672,
  0x5673,
  0x5674,
  0x5675,
  0x5778,
  0x5779,
  0x577a,
  0x577b,
  0x577c,
  0x577d,
  0x577e,
  0x577f,
  0x5788,
  0x5789,
  0x5901,
  0x5999,
  0x5b00,
  0x5b02,
  0x5b08,
  0x5b0a,
  0x5b0e,
  0x5b0f,
  0x5b12,
  0x5b14,
  0x5b18,
  0x5b1b,
  0x5b1d,
  0x5b22,
  0x5b25,
  0x5b26,
  0x5b28,
  0x5b29,
  0x5b2c,
  0x5b2d,
  0x5b30,
  0x5b31,
  0x5b34,
  0x5b35,
  0x5b38,
  0x5b39,
  0x5b3c,
  0x5b3d,
  0x5b41,
  0x5b42,
  0x5b45,
  0x5b46,
  0x5b4e,
  0x5b50,
  0x5b51,
  0x5b54,
  0x5b55,
  0x5b58,
  0x5b59,
  0x5b5c,
  0x5b5d,
  0x5c00,
  0x5c03,
  0x5c05,
  0x5c07,
  0x5c09,
  0x5c0b,
  0x5c0d,
  0x5c0f,
  0x5c10,
  0x5c11,
  0x5c14,
  0x5c16,
  0x5c17,
  0x5c19,
  0x5c1b,
  0x5c1d,
  0x5c1e,
  0x5c22,
  0x5c24,
  0x5c26,
  0x5c28,
  0x5c2a,
  0x5c2b,
  0x5c2d,
  0x5c2e,
  0x5c30,
  0x5c31,
  0x5c33,
  0x5c35,
  0x5c3d,
  0x5c40,
  0x7000,
  0x7001,
  0x7002,
  0x7003,
  0x7009,
  0x700a,
  0x700b,
  0x7051,
  0x7052,
  0x73ff,
  0x7400,
  0x7401,
  0x7402,
  0x7404,
  0x7405,
  0x7406,
  0x7407,
  0x7408,
  0x7409,
  0x740a,
  0x740b,
  0x740c,
  0x740d,
  0x740e,
  0x740f,
  0x7451,
  0x7453,
  0x7455,
  0x7457,
  0x7459,
  0x745b,
  0x745d,
  0x745f,
  0x7463,
  0x7465,
  0x7467,
  0x7469,
  0x746b,
  0x746d,
  0x746f,
  0x7471,
  0x7473,
  0x7701,
  0x7703,
  0x7707,
  0x770a,
  0x770d,
  0x770f,
  0x7711,
  0x7713,
  0x7719,
  0x771c,
  0x771f,
  0x7830,
  0x7833,
  0x7911,
  0x7912,
  0x7914,
  0x795e,
  0x796e,
  0x7974,
  0x7976,
  0x7977,
  0x7978,
  0x7979,
  0x7980,
  0x7982,
  0x79a0,
  0x79a2,
  0x79aa,
  0x79ac,
  0x79ae,
  0x79b0,
  0x79be,
  0x79c1,
  0x79c4,
  0x79c6,
  0x79c8,
  0x79ca,
  0x79cc,
  0x79ce,
  0x7a00,
  0x7a01,
  0x7a04,
  0x7a0f,
  0x7a14,
  0x7a24,
  0x7a27,
  0x7a40,
  0x7a43,
  0x7b02,
  0x7b07,
  0x7b08,
  0x7b0c,
  0x7b0e,
  0x8032,
  0x8033,
  0x8034,
  0x900b,
  0x900c,
  0x9010,
  0x9016,
  0x9017,
  0x9018,
  0x901a,
  0x901b,
  0x901e,
  0x9020,
  0x9022,
  0x9023,
  0x9025,
  0x902a,
  0x902e,
  0x9033,
  0x9035,
  0x9037,
  0x9039,
  0x903b,
  0x903d,
  0x903f,
  0x9041,
  0x904b,
  0x9050,
  0x9052,
  0x9055,
  0x9057,
  0x9059,
  0x905e,
  0x9101,
  0x9102,
  0x9107,
  0x9109,
  0x9132,
  0x9133,
  0x9139,
  0x913f,
  0x9141,
  0x9202,
  0x9204,
  0x9208,
  0x920b,
  0x920d,
  0x920f,
  0x9211,
  0x9213,
  0x9215,
  0x9217,
  0x9219,
  0x921b,
  0x921d,
  0x921f,
  0x9221,
  0x9223,
  0x9224,
  0x9225,
  0x9227,
  0x922a,
  0x922c,
  0x922e,
  0x9231,
  0x9233,
  0x9235,
  0x9304,
  0x930a,
  0x930c,
  0x930e,
  0x930f,
  0x9310,
  0x9401,
  0x9404,
  0x9406,
  0x9412,
  0x9414,
  0x9416,
  0x9418,
  0x941a,
  0x941c,
  0x941e,
  0x9420,
  0x9422,
  0x9424,
  0x9426,
  0x942a,
  0x942c,
  0x942f,
  0x9432,
  0x9434,
  0x9435,
  0x9503,
  0xd001,
  0xd006,
  0xd00b,
  0xd00e,
  0xd023,
  0xd024,
  0xd026,
  0xd028,
  0xd02a,
  0xd02c,
  0xd02d,
  0xd02e,
  0xd02f,
};

static const char* erd_user_selectable_names[] = {
  "Control User Interface Locked",
  "Clock Time - Hours",
  "Clock Format",
  "Temperature Display Units",
  "Sabbath Mode",
  "Sound Level",
  "Reset Board",
  "Service Mode State",
  "Enhanced Sabbath Mode Status",
  "Timer",
  "Hosted Firmware Status",
  "Alexa Registration Request - Request ID",
  "Matter Device On-Off Request - On-Off Request",
  "Matter Device Temperature Display Mode Request",
  "Matter Commissioning Mode Request - Request ID",
  "Video Stream",
  "Still Frame - Still Capture",
  "Request Enabled Enhanced Features",
  "Sound Theme Request",
  "Sign of Life",
  "UI Sound Level Request",
  "Desired Temperature - Fresh Food Desired Temperature",
  "Ice Maker Control - Freezer Status",
  "Humidity Control",
  "Quick Ice Status",
  "Turbo Freeze Status",
  "Turbo Cool Status",
  "Hot Water Desired Temperature",
  "Deli Pan Selection",
  "Deli Pan Desired Temperature - Deli Pan Position 1 Desired Temperature",
  "Altitude Status",
  "Hot Water Local Use",
  "Temperature Pan Current Setting - Temp Pan Mode 1 Setting",
  "Nighttime Snack Mode Status",
  "Nighttime Snack Mode Timeout",
  "Convertible Drawer Mode Selection",
  "Desired Adjustable Convertible Drawer Setting - Convertible Drawer Mode",
  "Fresh food cabinet backlight status",
  "Recess Light On when presence is sensed",
  "Presence sensing feature state",
  "Lock out feature state",
  "Display always on feature",
  "Ice Harvest Control",
  "Key Status - Key Mute",
  "Door Mute Status Upper Compartment",
  "Door Mute Status Lower Compartment",
  "Door Mute Status Middle Compartment",
  "Bottle Chill Alarm Upper Compartment",
  "Bottle Chill Alarm Lower Compartment",
  "Bottle Chill Alarm Middle Compartment",
  "Max Cool/Fast Freeze Upper Compartment",
  "Max Cool/Fast Freeze Lower Compartment",
  "Max Cool/Fast Freeze Middle Compartment",
  "Desired Temperature Setpoint (Kelvin) Upper Compartment",
  "Desired Temperature Setpoint (Kelvin) Lower Compartment",
  "Desired Temperature Setpoint (Kelvin) Middle Compartment",
  "Water Filter Reset Request",
  "Food Mode Upper Compartment (Desired)",
  "Food Mode Lower Compartment (Desired)",
  "Food Mode Middle Compartment (Desired)",
  "Desired Light Control Upper Compartment 1",
  "Desired Light Control Upper Compartment 2",
  "Desired Light Control Display",
  "Desired Light Control Lower Compartment",
  "Autofill pitcher feature request",
  "Food Mode Preset (Desired/Requested)",
  "Light Mode Control Desired Upper Compartment",
  "Light Mode Control Desired Lower Compartment",
  "Light Mode Control Desired Middle Compartment",
  "Eco Mode",
  "Water Filter Expiration Limits - Initial Gallons to Expiration",
  "Water Filter Reset Request",
  "Requested Water Valve Position",
  "Valve Manual Override Status",
  "Continuous Flow Time Limits - Low Limit Minutes",
  "Leak Validity Confirmation",
  "Unlock Request",
  "Go Box Mode Request",
  "Keypad Pin Number",
  "Allow Auto Water Valve Shut Off",
  "Turbo Cool Request",
  "Turbo Freeze Request",
  "Dimmable Lighting 0 Percent Level Request",
  "Presence Sensing Enable",
  "Presence Sensed Activates Recess Light",
  "Door Alarm Enable",
  "Night Time Snack Mode Lighting",
  "Kitchen Illumination Feature Enable",
  "Door Alarm Timer Timeout",
  "Barcode Scanner Feature",
  "Barcode Scanner Device Power",
  "Recess Light SBC Brightness",
  "Recess Light User Brightness Level Request",
  "Variable Cube Size",
  "Fridge Focus Camera Enable",
  "Grow State - Grow Requested State",
  "Displayed Grow Chamber",
  "Display Light Intensity Setting Request",
  "Mixing Spraying Data Request - Top Spray Period In Minutes",
  "Growing Data Integrity Request - Data Major",
  "Grow Profile Chamber 1 - Data[0]",
  "Grow Profile Chamber 2 - Data[0]",
  "Grow Profile Chamber 3 - Data[0]",
  "Water Intake Configuration",
  "Paused Mode - Paused Mode Request",
  "Reset Water Filter Timer Request - Request Type",
  "Reset Air Filter Timer Request - Request Type",
  "Reset Nutrient Cartridge Request - Request Type",
  "Clean Cycle Request - Request Type",
  "Barcode Scan History - Type",
  "SBC Request To Power Off Barcode Scanner",
  "Variable Cube Size 0",
  "Variable Cube Size 2",
  "Dimmable Lighting 0 Warmth Percentage Request",
  "System Power Off",
  "Scanned Barcode Result - Type",
  "Legacy - Washer Remote Soil Level Option",
  "Legacy - Washer Remote Wash Temperature Level",
  "Legacy Remote Spin Time Level",
  "Washer Remote Rinse Option",
  "Legacy - Dryer Extended Tumble Selection",
  "Connected Select Cycle",
  "Dryer Sheet Usage Configuration - Small Load Size Dryer Sheets Per Cycle",
  "Dryer Sheets Remaining",
  "Max Water Level",
  "Remote Set Delay Start Minutes",
  "Remote Stop Cycle Request",
  "Remote Start Extended Tumble",
  "EcoDry Option Request",
  "Damp Alert Option Request",
  "Dryer Dryness Level Request",
  "Dryer Dryness Option Selection",
  "Dryer Temperature Level Request",
  "Dryer Temperature Option Selection",
  "Dryer Extended Tumble Option Request",
  "Time Saver Option",
  "Steam Option Request",
  "Prewash Option",
  "Dryer Reduce Static Option Request",
  "Tumble Care Option Request",
  "Wash And Dry Option",
  "Dryer Small Load Option Request",
  "Washer Link Data - Washer Cycle Count",
  "Load Recommended Washer Link Cycle Request",
  "Washer Link Option Request",
  "Smart Detergent Dispense Flow Rate Bucket Selection",
  "Detangle Option Request",
  "Time Level Option Request",
  "Sanitize Option Request",
  "Key Volume Option Request",
  "Alert Volume Option Request",
  "Control Lock Option Request",
  "Steam Option Request",
  "Air Fluff Cycle Option Request",
  "Steam Cycle Option Request",
  "Warm Up Cycle Option Request",
  "Smart Vent Cycle Option Request",
  "Washer Link 2 Data - Washer Cycle Count",
  "Power Care Option",
  "Scent Option Request",
  "Remote Care Start Command",
  "Commercial Laundry Status External",
  "Color Keeper Option",
  "No Tangle Wash Option",
  "Remote Cycle Selection Request",
  "True Rinse Option",
  "Auto Soak Level Option",
  "Deep Fill Incremental Option",
  "Deep Fill Fixed Level Option",
  "Delay Wash Option",
  "Extra Rinse Option",
  "Fabric Softener Option",
  "Spin Level Option",
  "Water On Demand Soap Dispense Option",
  "Water Temperature Option",
  "Warm Rinse Option",
  "Soil Level Option",
  "Smart Dispense Adjustability Request - Substance Type",
  "Dry Time Option Request",
  "RJ45 Write Access Request",
  "Stain Removal Guide Option",
  "Downloaded Cycle",
  "Flex Dispense Request - Stage",
  "Adaptive My Cycle Option",
  "Washer Remote My Cycle Request - Request ID",
  "Active Intelligent Option Request",
  "Signal Option Request",
  "Timed Cycle Total Duration Option Request - Requested Minutes",
  "Eco Option",
  "Wash Mode Option",
  "Dry Mode Option",
  "Remote Spin Time Level",
  "EcoCool Option Request",
  "Remote Sensored Dry Only Cycle Start Command",
  "Timed Dry Option",
  "More Dry Option",
  "Smart Softener Dispense Flow Rate Bucket Selection",
  "Smart Softener Dispense Adjustability Request - Substance Type",
  "Remote Start Selected Cycle",
  "Soaking Rinse Option",
  "Wash complete cycle notification option Request",
  "Load Item Count Set Request",
  "Cycle Pricing - Cycle n Price[0]",
  "Option Pricing - Option n Price[0]",
  "Mid-Cycle Adjustment Pricing - Mid-Cycle Adjustment n Price[0]",
  "Smart Combi Option",
  "Reset Current Coin Box Count Request",
  "Smart Wash And Rinse Option Request",
  "Drum Light Option Request",
  "Delay Start Duration Request - Delay Start Duration Request In Minutes",
  "Additional Cavity Remote Set Delay Start Minutes",
  "Additional Cavity Remote Stop Cycle Request",
  "Additional Cavity Remote Cycle Selection Request",
  "Flexible Dispensing Tank 1 Configuration - Liquid Type",
  "Flexible Dispensing Tank 2 Configuration - Liquid Type",
  "Pet Hair Removal Option",
  "Commercial Remote Admin Mode Pin",
  "Commercial Remote Buzzer Disable",
  "Commercial Remote Wash Times - Cycle n Middle Setting Time in Minutes[0]",
  "Commercial Remote Wash Water Level",
  "Commercial Remote Wash Time In Seconds",
  "Commercial Remote Rinse Water Level",
  "Commercial Remote Extra Rinse Option Deselected By Default",
  "Commercial Remote Spin Limit Option - Option n Remote Spin Limit Option[0]",
  "Commercial Remote Dry Times - Option n Base Dry Time in Minutes[0]",
  "Commercial Remote Dry Temperature - Option n Drying Temperature[0]",
  "Commercial Remote Cooldown Times - Option n Cooldown Time in Minutes[0]",
  "Commercial Remote Cooldown Temperature - Option n Cooldown Temperature[0]",
  "Commercial Remote Max Drying Times - Option n Max Drying Time in Minutes[0]",
  "Commercial Remote Free Mode",
  "Mabe - Water Level Selection",
  "Mabe - Manual Selection",
  "Mabe - Soil Level Selection",
  "Mabe - Temperature Level Selection",
  "Mabe - Spin Level Selection",
  "Mabe - Loads Request - Lid lock Request",
  "Mabe - Run Application State",
  "Mabe - Main State",
  "Short Cycle Option",
  "PODS Count",
  "Rinse Aid Option State",
  "Door Pocket Light State",
  "Demo Mode State",
  "Smart Assist Cloud Notification",
  "Remote Control Command",
  "Water Softener Regeneration Frequency Setting Request",
  "Auto Lid Down Request",
  "Delay Start Value",
  "Selected Cycle Index",
  "Wash Temperature Setting",
  "Heated Dry Setting",
  "Wash Zone Selection",
  "Steam Option State",
  "Bottle Blast Option State",
  "UltraFresh Option State",
  "Silverware Wash Option State",
  "Custom Cycle Index",
  "Dish Favorite Settings",
  "Default to Eco Cycle Request",
  "Rinse and Hold Request",
  "Auto Open Door Request",
  "Tub 1 Remote Control Command",
  "Tub 1 Delay Start Value",
  "Tub 1 Selected Cycle Index",
  "Tub 1 Wash Temperature Setting",
  "Tub 1 Heated Dry Setting",
  "Mixing valve home state",
  "Water Heater User Mode",
  "Vacation Fallback Mode",
  "User Setpoint Temperature",
  "Vacation Setpoint Temperature",
  "Timed Mode Hours Remaining",
  "Water Heater Mixing Valve Tank Capacity",
  "Min/max allowed setpoint - Minimum setpoint",
  "Min/max allowed vacation setpoint - Minimum setpoint",
  "Water Heater Dad Mode Delay In Minutes",
  "Water Heater Boost Mode State - Requested/Desired",
  "Requested Water Valve Position",
  "Water Heater Active State - Requested/Desired",
  "Twelve Hour Shutoff",
  "End Tone",
  "Convection Conversion",
  "Warming Drawer Power Level",
  "Knob Mode Setting",
  "Auto Oven Light On Off Request",
  "Broil Level",
  "Clock Display",
  "Accent Lighting - Custom Color Active",
  "Articulating Display Mode",
  "Articulating Display Position",
  "Automatic Door Opener Local Public Enable",
  "Oven Light Auto On Off State",
  "Enhanced Sabbath Cooking Accepted",
  "Enhanced Sabbath Warmness Setting",
  "Cook Cam AI Assistant Enabled",
  "Cook Cam Image Upload Enabled Setting",
  "AI Bake Cloud Status",
  "Heartbeat Tick From Client",
  "Upper Oven Cook Mode Command - Mode",
  "Upper Oven Display Timer",
  "Upper Oven User Temperature Offset",
  "Request Upper Door Open",
  "Upper Oven Light Level",
  "Upper Oven Do Not Stop Cook Mode On Timer Expiration",
  "Upper Oven Cook Time Add Or Subtract - Request ID",
  "Upper Cavity Cook Recipe - Cook Action",
  "Upper Cavity Cook Recipe Stage 1 - Request ID",
  "Upper Cavity Cook Recipe Stage 2 - Request ID",
  "Upper Cavity Cook Recipe Stage 3 - Request ID",
  "Upper Cavity Cook Recipe Stage 4 - Request ID",
  "Upper Cavity Cook Recipe Stage 5 - Request ID",
  "Upper Oven Advance Cook Stage",
  "Lower Oven Cook Mode - Mode",
  "Lower Oven Display Timer",
  "Lower Oven User Temperature Offset",
  "Request Lower Door Open",
  "Lower Oven Light Level",
  "Lower Oven Do Not Stop Cook Mode On Timer Expiration",
  "Lower Oven Cook Time Add Or Subtract - Request ID",
  "Lower Cavity Cook Recipe - Cook Action",
  "Lower Cavity Cook Recipe Stage 1 - Request ID",
  "Lower Cavity Cook Recipe Stage 2 - Request ID",
  "Lower Cavity Cook Recipe Stage 3 - Request ID",
  "Lower Cavity Cook Recipe Stage 4 - Request ID",
  "Lower Cavity Cook Recipe Stage 5 - Request ID",
  "Lower Oven Advance Cook Stage",
  "Advantium Cook - Remote Cook Request ID",
  "Advantium Cook Time Adjustment - Request ID",
  "Advantium Menu Tree Selection",
  "Precision Cook Mode UID Cavity 1 Request",
  "Precision Cook Mode UID Cavity 2 Request",
  "Primary Oven Remote Precision Cook UI Configuration - Precision Cook Probe Required",
  "Advantium Remote JSON Cook - Cook Action",
  "Secondary Oven Remote Precision Cook UI Configuration - Precision Cook Probe Required",
  "Closed-Loop Cooking Target Temperature",
  "Closed-Loop Cooking Current Temperature",
  "Target Closed-Loop Cooking Time",
  "Start Closed-Loop Cooking Cook Timer",
  "Left Front Closed-Loop Cooking Configuration - Target Temperature",
  "Left Rear Closed-Loop Cooking Configuration - Target Temperature",
  "Center Front (Left-Center Front) Closed-Loop Cooking Configuration - Target Temperature",
  "Center Rear (Left-Center Rear) Closed-Loop Cooking Configuration - Target Temperature",
  "Right Front Closed-Loop Cooking Configuration - Target Temperature",
  "Right Rear Closed-Loop Cooking Configuration - Target Temperature",
  "Bridge/Sync Closed-Loop Cooking Configuration - Target Temperature",
  "Set Pan Type",
  "Right-Center Front Closed-Loop Cooking Configuration - Target Temperature",
  "Right-Center Rear Closed-Loop Cooking Configuration - Target Temperature",
  "Lock Gas Valve Request",
  "MCT User Settings Request",
  "Hood Fan Speed",
  "Hood Light Level",
  "Hood Camera Light Assist Level",
  "Hood Display Protection Fan Speed",
  "Boost Duration",
  "Delay Off Duration",
  "Hood Request On/Off",
  "Hood Requested Fan Speed",
  "Hood Requested Light Level",
  "Hood Delay Off",
  "Hood Requested Delay Off Duration",
  "Hood Requested Light Color",
  "Hood to Hob Request Auto Control",
  "Hood Requested Auto Fan Speed",
  "Requested Relative Humidity Sensor Idle Update Period in Minutes",
  "Request Update of Relative Humidity (%)",
  "Requested VOC Index Sensor Update Idle Period in Minutes",
  "Request Update of VOC Index",
  "Requested PM 2.5 Sensor Idle Update Period in Minutes",
  "Request Update of PM 2.5 (ug/m^3)",
  "Requested NOx Sensor Idle Update Period in Minutes",
  "Request Update of NOx Index",
  "Requested PM 1 Sensor Idle Update Period in Minutes",
  "Request Update of PM 1 (ug/m^3)",
  "Requested PM 4 Sensor Idle Update Period in Minutes",
  "Request Update of PM 4 (ug/m^3)",
  "Requested PM 10 Sensor Idle Update Period in Minutes",
  "Request Update of PM 10 (ug/m^3)",
  "Requested Environmental Temperature Idle Update Period in Minutes",
  "Request Update of Environmental Temperature (F)",
  "Requested Color Scheme",
  "Requested PM 2.5 Index Sensor Update Period in Minutes",
  "Request Update of PM 2.5 Index",
  "Requested PM 1 Index Sensor Update Period in Minutes",
  "Request Update of PM 1 Index",
  "Requested PM 4 Index Sensor Update Period in Minutes",
  "Request Update of PM 4 Index",
  "Requested PM 10 Index Sensor Update Period in Minutes",
  "Request Update of PM 10 Index",
  "Microwave Timed Mode Current Setting - Power Level",
  "Microwave Popcorn Mode - Current Setting",
  "Microwave Potato Mode - Current Setting",
  "Microwave Beverage Mode - Current Setting",
  "Microwave Pizza Mode - Current Setting",
  "Microwave Frozen Vegetable Mode - Current Setting",
  "Microwave Fresh Vegetable Mode - Current Setting",
  "Microwave Reheat Mode - Current Setting",
  "Microwave Add 30 Second Command - Add 30 Seconds Feature",
  "Microwave State - State",
  "Microwave Remote Enable",
  "Microwave Defrost by Weight - Current Setting",
  "Microwave Defrost by Time Mode - Current Setting - Power Level",
  "Microwave Kitchen/Display Timer - Minutes",
  "Microwave Cook Timer Modification - Add or Subtract Cook Time",
  "Microwave Dinner Plate Mode - Current Setting",
  "Microwave Kitchen Timer Modification - Add or Subtract Kitchen Timer Time",
  "Microwave Warm Mode - Current Setting",
  "Microwave Auto Defrost Mode - Current Setting - Auto Defrost Mode Current Setting",
  "Microwave Broil Mode - Current Setting",
  "Microwave Auto Bake Mode - Current Setting - Current Mode",
  "Microwave Auto Roast Mode - Current Setting - Current Mode",
  "Microwave Oven Cook Timer - Hours",
  "Microwave Oven Temperature Range - Convection Bake Minimum Temperature",
  "Microwave Protein Mode - Current Setting - Protein Type current Setting",
  "Microwave Broil With Time Mode - Current Setting",
  "Turntable Setting",
  "Microwave Melt Mode - Current Setting - Melt Type Current Setting",
  "Microwave Steam Cook Mode - Current Setting - Steam Cook Current Setting",
  "Staged Microwave Cook Request - Cook Action",
  "Customer Feedback",
  "System Mode",
  "Zoneline On/Off Control",
  "Target Heating Temperature",
  "Target Cooling Temperature",
  "Cooling Fan Mode",
  "Fan-Only Fan Mode",
  "Heating Fan Mode",
  "Air Purifier - Air Purifier Off",
  "Energy Conservation",
  "UVC Kit Enable",
  "Dehumidification Mode",
  "Smart Fan Cooling",
  "Smart Fan Heating",
  "Freeze Sentinel",
  "Heat Sentinel",
  "Constant Fan State",
  "Cool Temperature Limit Mode",
  "Heat Temperature Limit Mode",
  "External Thermostat",
  "Duct Mode",
  "Electric Heat Only mode",
  "Boost Heat Mode",
  "MUAM Fan Speed setting",
  "MUAM Occupancy Enabled",
  "Engineering Revision setup",
  "Fan Configuration in Cooling",
  "Fan Configuration in Heating",
  "Freeze Sentinel",
  "Heat Sentinel",
  "Constant Fan",
  "24V External Thermostat",
  "Fan Boost",
  "Heat Selector",
  "UVC Module",
  "Make-up Air Fan Cfm",
  "Make-up Air Filter Type",
  "Make-up Air Occupancy Control",
  "Dehumidification Mode",
  "Auxiliary 24V Configuration Request",
  "Heat Pump Lockout Temperature",
  "Input Current Limiting",
  "Heat Source Optimization",
  "Power",
  "System Mode",
  "User Heating Setpoint",
  "User Cooling Setpoint",
  "User Heating Setpoint Minimum Limit",
  "User Heating Setpoint Maximum Limit",
  "User Cooling Setpoint Minimum Limit",
  "User Cooling Setpoint Maximum Limit",
  "Heat System Mode Fan Setting",
  "Fan System Mode Fan Setting",
  "Cool System Mode Fan Setting",
  "Dehumidifier Pump On/Off State Request",
  "Dehumidifier Nonstop Mode",
  "ODU Pan Heater status and control",
  "Compressor Crankcase Heater status and control",
  "Defrost status and control",
  "Turbo/Quiet Mode Modifier",
  "Self Clean Mode Status and Control",
  "External Damper",
  "Vacation Mode (10C Heating Mode) Control",
  "Service Mode Electric Room Heater Request",
  "Self Clean Request",
  "Self Clean Reminder Timer Duration Selection",
  "Temperature Display Mode Selection",
  "Motion Sensing Mode Selection - Selected Occupancy Standby Timer",
  "Filter Replacement Interval Reminder",
  "Local Schedule Enable Request",
  "Auto Mode Temperature Deadband",
  "Compressor Minimum Runtime",
  "Compressor Minimum Idle Time",
  "Compressor Maximum Stage1 Runtime",
  "Fan Operating Mode",
  "Indoor Temperature Chassis/Install Adjustment",
  "Compressor Minimum Stage2 Temperature Delta",
  "Blower Minimum Runtime",
  "Indoor Fan Delay",
  "Minimum Heat Time",
  "Auxiliary Heat Minimum Temperature Delta",
  "Indoor Ambient Temperature Sensor Calibration",
  "WAC Fan Setting",
  "WAC Operation Mode",
  "WAC Filter Notification",
  "WAC Power On/Off State",
  "WAC Save Energy Time",
  "Matter Thermostat Cluster Occupied Cooling Setpoint Request",
  "Matter Thermostat Cluster System Mode Request",
  "Matter Fan Control Cluster Fan Mode Request",
  "Matter Fan Control Cluster Percent Setting Request",
  "Target Humidity",
  "Up-down Air Swing",
  "Left-right Air Swing",
  "Eco Mode",
  "Sleep Mode",
  "Water Softener Shutoff Valve Installed",
  "Water Softener Shutoff Valve Actual State",
  "Water Softener Shutoff Valve Requested State",
  "Coffee Brewer Start Brew - Strength",
  "Coffee Brewer Cancel Brew",
  "Coffee Brewer Start Custom Brew - Temperature",
  "Coffee Brewer Brew Strength",
  "Coffee Brewer Brew Temperature",
  "Coffee Brewer Brew Cups",
  "Coffee Brewer Start Descale",
  "Coffee Brewer Cancel Descale",
  "Espresso Maker Requested Brew Parameters - Temperature",
  "Espresso Maker Requested Steam Duration Parameter",
  "Espresso Maker Requested Rinse Water Volume Parameters",
  "Espresso Maker Reset Brew Parameters",
  "Espresso Maker Requested Standby Timeout",
  "Espresso Maker Requested Americano Brew Parameters - Temperature",
  "Espresso Maker Set the Water Hardness Setting",
  "Espresso Maker Requested Lungo Brew Parameters - Temperature",
  "Espresso Maker Requested Espresso Brew Parameters - Temperature",
  "Espresso Maker Requested Hot Water Brew Parameters - Temperature",
  "Espresso Maker Requested Steam Brew Parameters - Temperature",
  "Espresso Maker Requested Ristretto Brew Parameters - Temperature",
  "Espresso Maker Requested Doppio Brew Parameters - Temperature",
  "Espresso Maker Requested Triple Shot Brew Parameters - Temperature",
  "Espresso Maker Requested Red Eye Brew Parameters - Temperature",
  "Espresso Maker Requested MyCup/MyBrew Parameters - Temperature",
  "Disable Grinder Requested State",
  "Coffee Brewer Request Brew Settings - Setting Gold",
  "Coffee Brewer Favorite Brew 1 Request Settings - Setting Gold",
  "Coffee Brewer Favorite Brew 2 Request Settings - Setting Gold",
  "Coffee Brewer Favorite Brew 3 Request Settings - Setting Gold",
  "Delay Start Request - Delay Start Enabled",
  "Ice Maker Light",
  "Ice Maker Cloud Schedule Enabled",
  "Ice Maker Power",
  "Ice Maker Cube Size",
  "Tank Drain Frequency Setting",
  "Harvest Time Adjustment Setting",
  "Requested Filter Type",
  "Light Color - Intensity Percentage",
  "Water Level Tone Setting Requested State",
  "Toaster Oven Cavity Light Requested State",
  "SDA Requested Display Brightness Setting",
  "Toaster Oven Requested Cooking Parameters - Shade",
  "Toaster Oven Requested Air Fry Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Bake Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Broil Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Roast Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Reheat Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Warm Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Slow Cook Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Dehydrate Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Proof Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Cookie Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Pizza Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Bagel Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Toast Mode Cooking Parameters - Shade",
  "Toaster Oven Door Alarm Silence",
  "Toaster Oven Reset Cooking Parameters for the Selected Cooking Mode",
  "Toaster Oven Requested Crisp Finish Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Preheat Enabled Setting",
  "Toaster Oven Requested Convection Fan State",
  "Toaster Oven Add More Time - Add Cook Time Seconds",
  "Toaster Oven Requested Cake Mode Cooking Parameters - Shade",
  "Toaster Oven Requested Cookie Mode with Preferences Cooking Parameters - Shade",
  "Toaster Oven Requested Pizza Mode with Preferences Cooking Parameters - Shade",
  "Mixer Pause Mixing Cycle",
  "Cycle Timer Absolute Time Modification - Add or Subtract Cycle Time",
  "Scale Mode Requested Units",
  "Scale Requested Target Weight",
  "Scale Mode Enable",
  "Tare Scale",
  "Smoker Cooking - State Requested",
  "Smoke Request",
  "Smoke Paused Request",
  "Custom Mode Requested Parameters - Cavity Temperature",
  "Keep Warm Mode Requested Parameters - Cavity Temperature",
  "Brisket Mode Requested Parameters - Cavity Temperature",
  "Pork Rib Mode Requested Parameters - Cavity Temperature",
  "Pork Butt Mode Requested Parameters - Cavity Temperature",
  "Wings Mode Requested Parameters - Cavity Temperature",
  "Chicken Mode Requested Parameters - Cavity Temperature",
  "Salmon Mode Requested Parameters - Cavity Temperature",
  "Auto Warm Enable Requested Setting",
  "Auto Warm Requested Time",
  "Auto Warm Requested Temperature",
  "Auto Warm Reminder Requested Setting",
  "Early Completion Notification Temperature Threshold",
  "Early Completion Notification Time Threshold",
  "Requested Igniter Temperature",
  "Reset Cooking Parameters",
  "Restore Factory Defaults",
  "Temperature Offset",
  "DSM Override Status",
  "Demand Response Status",
  "Temporary Temperature Offset Demand Response - Segment Count",
  "Cost of power cost/comfort slider",
  "Electrical Pricing Structure Selection",
  "Electrical Time of Use Pricing Schedule Request - Season 1 - Maximum Pricing Tier Value",
  "Electrical Time of Use Pricing Schedule Request - Season 2 - Maximum Pricing Tier Value",
  "Electrical Time of Use Pricing Schedule Request - Season 3 - Maximum Pricing Tier Value",
  "Electrical Time of Use Pricing Schedule Request - Season 4 - Maximum Pricing Tier Value",
  "Requested Appliance Energy Usage Update Period in Minutes",
  "Requested Appliance Hot Water Usage Update Period in Minutes",
  "Requested Appliance Cold Water Usage Update Period in Minutes",
  "Requested Appliance Gas Usage Update Period in Minutes",
};

static const char* erd_user_selectable_name(uint16_t erd) {
  uint16_t lo = 0, hi = ERD_USER_SELECTABLE_COUNT;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    if (erd_user_selectable_ids[mid] < erd) lo = mid + 1;
    else if (erd_user_selectable_ids[mid] > erd) hi = mid;
    else return erd_user_selectable_names[mid];
  }
  return NULL;
}

/* Decode a user-selectable ERD value to a human-readable string. */
/* Returns a pointer to a static buffer (overwritten on next call). */
static const char* erd_user_selectable_decode(uint16_t erd, const uint8_t* data, uint8_t data_size) {
  static char result[64];
  uint16_t lo = 0, hi = ERD_USER_SELECTABLE_COUNT;
  while (lo < hi) {
    uint16_t mid = lo + (hi - lo) / 2;
    if (erd_user_selectable_ids[mid] < erd) lo = mid + 1;
    else if (erd_user_selectable_ids[mid] > erd) hi = mid;
    else {
      switch (mid) {
      case 0: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 1: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 2: {
        if (data_size >= 1 && data[0] == 0x00) return "12 Hour Time";
        if (data_size >= 1 && data[0] == 0x01) return "24 Hour Time";
        if (data_size >= 1 && data[0] == 0x02) return "No Clock Display";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 3: {
        if (data_size >= 1 && data[0] == 0x00) return "Fahrenheit";
        if (data_size >= 1 && data[0] == 0x01) return "Celsius";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 4: {
        if (data_size >= 1 && data[0] == 0x00) return "Disable";
        if (data_size >= 1 && data[0] == 0x01) return "Enable";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 5: {
        if (data_size >= 1 && data[0] == 0x00) return "Off/Mute";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 6: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 7: {
        if (data_size >= 1 && data[0] == 0x00) return "Service mode inactive";
        if (data_size >= 1 && data[0] == 0x01) return "Service mode active";
        if (data_size >= 1 && data[0] == 0x02) return "Entering service mode";
        if (data_size >= 1 && data[0] == 0x03) return "Exiting service mode";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 8: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 9: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 10: {
        if (data_size >= 1 && data[0] == 0x00) return "Idle";
        if (data_size >= 1 && data[0] == 0x01) return "Upgrade";
        if (data_size >= 1 && data[0] == 0x02) return "Image Error";
        if (data_size >= 1 && data[0] == 0x03) return "Write Error";
        if (data_size >= 1 && data[0] == 0x04) return "Downloading";
        if (data_size >= 1 && data[0] == 0x05) return "Appliance Busy";
        if (data_size >= 1 && data[0] == 0x06) return "Incompatible Update";
        if (data_size >= 1 && data[0] == 0x07) return "Delayed Upgrade";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 11: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 12: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 13: {
        if (data_size >= 1 && data[0] == 0x00) return "Celsius";
        if (data_size >= 1 && data[0] == 0x01) return "Fahrenheit";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 14: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 15: {
        if (data_size >= 1 && data[0] == 0x00) return "Stopped";
        if (data_size >= 1 && data[0] == 0x01) return "Running 720p";
        if (data_size >= 1 && data[0] == 0x02) return "Running 480p";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 16: {
        if (data_size >= 1 && data[0] == 0x00) return "Idle";
        if (data_size >= 1 && data[0] == 0x01) return "Capture";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 17: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 18: {
        if (data_size >= 1 && data[0] == 0x00) return "Default";
        if (data_size >= 1 && data[0] == 0x01) return "Classical";
        if (data_size >= 1 && data[0] == 0x02) return "Morse Code";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 19: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 20: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 21: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 22: {
        if (data_size >= 1 && data[0] == 0x00) return "Freezer Ice Maker Off";
        if (data_size >= 1 && data[0] == 0x01) return "Freezer Ice Maker On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 23: {
        if (data_size >= 1 && data[0] == 0x00) return "Normal";
        if (data_size >= 1 && data[0] == 0x01) return "Boost";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 24: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Equipped";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 25: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Equipped";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 26: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Equipped";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 27: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 28: {
        if (data_size >= 1 && data[0] == 0x01) return "Deli Pan Position 1";
        if (data_size >= 1 && data[0] == 0x02) return "Deli Pan Position 2";
        if (data_size >= 1 && data[0] == 0x03) return "Deli Pan Position 3";
        if (data_size >= 1 && data[0] == 0x04) return "Deli Pan Position 4";
        if (data_size >= 1 && data[0] == 0x05) return "Deli Pan Position 5";
        if (data_size >= 1 && data[0] == 0xff) return "Not Controllable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 29: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 30: {
        if (data_size >= 1 && data[0] == 0x00) return "Low Altitude";
        if (data_size >= 1 && data[0] == 0x01) return "High Altitude";
        if (data_size >= 1 && data[0] == 0xff) return "Unknown";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 31: {
        if (data_size >= 1 && data[0] == 0x00) return "Available";
        if (data_size >= 1 && data[0] == 0x01) return "In Use";
        if (data_size >= 1 && data[0] == 0xff) return "Feature Unavailable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 32: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Setting 1";
        if (data_size >= 1 && data[0] == 0x02) return "Setting 2";
        if (data_size >= 1 && data[0] == 0x03) return "Setting 3";
        if (data_size >= 1 && data[0] == 0x04) return "Setting 4";
        if (data_size >= 1 && data[0] == 0x05) return "Setting 5";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 33: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 34: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 35: {
        if (data_size >= 1 && data[0] == 0x01) return "Convertible Drawer Position 1";
        if (data_size >= 1 && data[0] == 0x02) return "Convertible Drawer Position 2";
        if (data_size >= 1 && data[0] == 0x03) return "Convertible Drawer Position 3";
        if (data_size >= 1 && data[0] == 0x04) return "Convertible Drawer Position 4";
        if (data_size >= 1 && data[0] == 0x05) return "Convertible Drawer Position 5";
        if (data_size >= 1 && data[0] == 0x06) return "Convertible Drawer Position 6";
        if (data_size >= 1 && data[0] == 0x07) return "Convertible Drawer Position 7";
        if (data_size >= 1 && data[0] == 0xff) return "Not Controllable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 36: {
        if (data_size >= 1 && data[0] == 0x01) return "Convertible Drawer Mode 1";
        if (data_size >= 1 && data[0] == 0x02) return "Convertible Drawer Mode 2";
        if (data_size >= 1 && data[0] == 0x03) return "Convertible Drawer Mode 3";
        if (data_size >= 1 && data[0] == 0x04) return "Convertible Drawer Mode 4";
        if (data_size >= 1 && data[0] == 0xff) return "Not Controllable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 37: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 38: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Enabled";
        if (data_size >= 1 && data[0] == 0xff) return "Not Available";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 39: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Enabled";
        if (data_size >= 1 && data[0] == 0xff) return "Not Available";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 40: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 41: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 42: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop Ice Harvest";
        if (data_size >= 1 && data[0] == 0x01) return "Start Ice Harvest";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 43: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 44: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 45: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 46: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 47: {
        if (data_size >= 1 && data[0] == 0x00) return "Timer Off";
        if (data_size >= 1 && data[0] == 0x01) return "Timer On";
        if (data_size >= 1 && data[0] == 0x02) return "Timer Expired";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 48: {
        if (data_size >= 1 && data[0] == 0x00) return "Timer Off";
        if (data_size >= 1 && data[0] == 0x01) return "Timer On";
        if (data_size >= 1 && data[0] == 0x02) return "Timer Expired";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 49: {
        if (data_size >= 1 && data[0] == 0x00) return "Timer Off";
        if (data_size >= 1 && data[0] == 0x01) return "Timer On";
        if (data_size >= 1 && data[0] == 0x02) return "Timer Expired";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 50: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 51: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 52: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 53: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 54: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 55: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 56: {
        if (data_size >= 1 && data[0] == 0x00) return "No Request";
        if (data_size >= 1 && data[0] == 0x01) return "Reset Requested";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 57: {
        if (data_size >= 1 && data[0] == 0x00) return "Fridge";
        if (data_size >= 1 && data[0] == 0x01) return "Pantry";
        if (data_size >= 1 && data[0] == 0x02) return "Chill";
        if (data_size >= 1 && data[0] == 0x03) return "Freezer";
        if (data_size >= 1 && data[0] == 0x04) return "Soft Freeze";
        if (data_size >= 1 && data[0] == 0x05) return "Deep Freeze";
        if (data_size >= 1 && data[0] == 0x06) return "Cellar Wine";
        if (data_size >= 1 && data[0] == 0x07) return "Red Wine";
        if (data_size >= 1 && data[0] == 0x08) return "White Wine";
        if (data_size >= 1 && data[0] == 0x09) return "Sparkling Wine";
        if (data_size >= 1 && data[0] == 0x0a) return "Beverage";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 58: {
        if (data_size >= 1 && data[0] == 0x00) return "Fridge";
        if (data_size >= 1 && data[0] == 0x01) return "Pantry";
        if (data_size >= 1 && data[0] == 0x02) return "Chill";
        if (data_size >= 1 && data[0] == 0x03) return "Freezer";
        if (data_size >= 1 && data[0] == 0x04) return "Soft Freeze";
        if (data_size >= 1 && data[0] == 0x05) return "Deep Freeze";
        if (data_size >= 1 && data[0] == 0x06) return "Cellar Wine";
        if (data_size >= 1 && data[0] == 0x07) return "Red Wine";
        if (data_size >= 1 && data[0] == 0x08) return "White Wine";
        if (data_size >= 1 && data[0] == 0x09) return "Sparkling Wine";
        if (data_size >= 1 && data[0] == 0x0a) return "Beverage";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 59: {
        if (data_size >= 1 && data[0] == 0x00) return "Fridge";
        if (data_size >= 1 && data[0] == 0x01) return "Pantry";
        if (data_size >= 1 && data[0] == 0x02) return "Chill";
        if (data_size >= 1 && data[0] == 0x03) return "Freezer";
        if (data_size >= 1 && data[0] == 0x04) return "Soft Freeze";
        if (data_size >= 1 && data[0] == 0x05) return "Deep Freeze";
        if (data_size >= 1 && data[0] == 0x06) return "Cellar Wine";
        if (data_size >= 1 && data[0] == 0x07) return "Red Wine";
        if (data_size >= 1 && data[0] == 0x08) return "White Wine";
        if (data_size >= 1 && data[0] == 0x09) return "Sparkling Wine";
        if (data_size >= 1 && data[0] == 0x0a) return "Beverage";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 60: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 61: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 62: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 63: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 64: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 65: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 66: {
        if (data_size >= 1 && data[0] == 0x00) return "Light Mode Off";
        if (data_size >= 1 && data[0] == 0x01) return "Light Mode Low";
        if (data_size >= 1 && data[0] == 0x02) return "Light Mode Med";
        if (data_size >= 1 && data[0] == 0x03) return "Light Mode High";
        if (data_size >= 1 && data[0] == 0x04) return "Light Mode Feature";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 67: {
        if (data_size >= 1 && data[0] == 0x00) return "Light Mode Off";
        if (data_size >= 1 && data[0] == 0x01) return "Light Mode Low";
        if (data_size >= 1 && data[0] == 0x02) return "Light Mode Med";
        if (data_size >= 1 && data[0] == 0x03) return "Light Mode High";
        if (data_size >= 1 && data[0] == 0x04) return "Light Mode Feature";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 68: {
        if (data_size >= 1 && data[0] == 0x00) return "Light Mode Off";
        if (data_size >= 1 && data[0] == 0x01) return "Light Mode Low";
        if (data_size >= 1 && data[0] == 0x02) return "Light Mode Med";
        if (data_size >= 1 && data[0] == 0x03) return "Light Mode High";
        if (data_size >= 1 && data[0] == 0x04) return "Light Mode Feature";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 69: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 70: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 71: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 72: {
        if (data_size >= 1 && data[0] == 0x00) return "Bypass";
        if (data_size >= 1 && data[0] == 0x01) return "Closed";
        if (data_size >= 1 && data[0] == 0x02) return "Open/Filtered";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 73: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 74: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 75: {
        if (data_size >= 1 && data[0] == 0x00) return "Leak Confirmation Inactive";
        if (data_size >= 1 && data[0] == 0x01) return "Leak Invalid/Rejected";
        if (data_size >= 1 && data[0] == 0x02) return "Leak Valid/Confirmed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 76: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 77: {
        if (data_size >= 1 && data[0] == 0x00) return "Standby";
        if (data_size >= 1 && data[0] == 0x01) return "Fresh";
        if (data_size >= 1 && data[0] == 0x02) return "Freeze";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 78: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 79: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 80: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 81: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 82: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 83: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 84: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 85: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 86: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 87: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 88: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 89: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 90: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 91: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 92: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 93: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 94: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 95: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 96: {
        if (data_size >= 1 && data[0] == 0x00) return "Stopped Unknown Section";
        if (data_size >= 1 && data[0] == 0x01) return "Section 1";
        if (data_size >= 1 && data[0] == 0x02) return "Section 2";
        if (data_size >= 1 && data[0] == 0x03) return "Section 3";
        if (data_size >= 1 && data[0] == 0x04) return "Stopped between Section 1 and 2";
        if (data_size >= 1 && data[0] == 0x05) return "Stopped between Section 2 and 3";
        if (data_size >= 1 && data[0] == 0x06) return "Stopped between Section 3 and 1";
        if (data_size >= 1 && data[0] == 0x07) return "Moving";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 97: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 98: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 99: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 100: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 101: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 102: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 103: {
        if (data_size >= 1 && data[0] == 0x00) return "Water Tank";
        if (data_size >= 1 && data[0] == 0x01) return "Water Supply Line";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 104: {
        if (data_size >= 1 && data[0] == 0x00) return "Exit";
        if (data_size >= 1 && data[0] == 0x01) return "Enter";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 105: {
        if (data_size >= 1 && data[0] == 0x00) return "No action required";
        if (data_size >= 1 && data[0] == 0x01) return "Reset Filter";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 106: {
        if (data_size >= 1 && data[0] == 0x00) return "No action required";
        if (data_size >= 1 && data[0] == 0x01) return "Reset Filter";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 107: {
        if (data_size >= 1 && data[0] == 0x00) return "No action required";
        if (data_size >= 1 && data[0] == 0x01) return "Reset Filter";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 108: {
        if (data_size >= 1 && data[0] == 0x00) return "No action required";
        if (data_size >= 1 && data[0] == 0x01) return "Start Clean Cycle";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 109: {
        if (data_size >= 1 && data[0] == 0x00) return "CodeId1Mappings_Code128";
        if (data_size >= 1 && data[0] == 0x01) return "CodeId1Mappings_Ean8";
        if (data_size >= 1 && data[0] == 0x02) return "CodeId1Mappings_Ean13";
        if (data_size >= 1 && data[0] == 0x03) return "CodeId1Mappings_Issn";
        if (data_size >= 1 && data[0] == 0x04) return "CodeId1Mappings_Issb";
        if (data_size >= 1 && data[0] == 0x05) return "CodeId1Mappings_Upce";
        if (data_size >= 1 && data[0] == 0x06) return "CodeId1Mappings_Upca";
        if (data_size >= 1 && data[0] == 0x07) return "CodeId1Mappings_Interleaved2of5";
        if (data_size >= 1 && data[0] == 0x08) return "CodeId1Mappings_Itf6";
        if (data_size >= 1 && data[0] == 0x09) return "CodeId1Mappings_Itf14";
        if (data_size >= 1 && data[0] == 0x0a) return "CodeId1Mappings_Matrix2of5";
        if (data_size >= 1 && data[0] == 0x0b) return "CodeId1Mappings_Industrial2of5";
        if (data_size >= 1 && data[0] == 0x0c) return "CodeId1Mappings_Code39";
        if (data_size >= 1 && data[0] == 0x0d) return "CodeId1Mappings_Codabar";
        if (data_size >= 1 && data[0] == 0x0e) return "CodeId1Mappings_Code93";
        if (data_size >= 1 && data[0] == 0x0f) return "CodeId1Mappings_Code11";
        if (data_size >= 1 && data[0] == 0x10) return "CodeId1Mappings_Msi_Plessey";
        if (data_size >= 1 && data[0] == 0x11) return "CodeId1Mappings_Rss14";
        if (data_size >= 1 && data[0] == 0x12) return "CodeId1Mappings_Rss14Limited";
        if (data_size >= 1 && data[0] == 0x13) return "CodeId1Mappings_Rss14Expanded";
        if (data_size >= 1 && data[0] == 0x14) return "CodeId1Mappings_Rss14Stacked";
        if (data_size >= 1 && data[0] == 0x15) return "CodeId1Mappings_Pdf417";
        if (data_size >= 1 && data[0] == 0x16) return "CodeId1Mappings_DataMatrix";
        if (data_size >= 1 && data[0] == 0x17) return "CodeId1Mappings_QRCode";
        if (data_size >= 1 && data[0] == 0x18) return "CodeId1Mappings_AztecCode";
        if (data_size >= 1 && data[0] == 0x19) return "CodeId1Mappings_Maxicode";
        if (data_size >= 1 && data[0] == 0x1a) return "CodeId1Mappings_MicroPdf417";
        if (data_size >= 1 && data[0] == 0x1b) return "CodeId1Mappings_MicroQR";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 110: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 111: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 112: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 113: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 114: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 115: {
        if (data_size >= 1 && data[0] == 0x00) return "CodeId1Mappings_Code128";
        if (data_size >= 1 && data[0] == 0x01) return "CodeId1Mappings_Ean8";
        if (data_size >= 1 && data[0] == 0x02) return "CodeId1Mappings_Ean13";
        if (data_size >= 1 && data[0] == 0x03) return "CodeId1Mappings_Issn";
        if (data_size >= 1 && data[0] == 0x04) return "CodeId1Mappings_Issb";
        if (data_size >= 1 && data[0] == 0x05) return "CodeId1Mappings_Upce";
        if (data_size >= 1 && data[0] == 0x06) return "CodeId1Mappings_Upca";
        if (data_size >= 1 && data[0] == 0x07) return "CodeId1Mappings_Interleaved2of5";
        if (data_size >= 1 && data[0] == 0x08) return "CodeId1Mappings_Itf6";
        if (data_size >= 1 && data[0] == 0x09) return "CodeId1Mappings_Itf14";
        if (data_size >= 1 && data[0] == 0x0a) return "CodeId1Mappings_Matrix2of5";
        if (data_size >= 1 && data[0] == 0x0b) return "CodeId1Mappings_Industrial2of5";
        if (data_size >= 1 && data[0] == 0x0c) return "CodeId1Mappings_Code39";
        if (data_size >= 1 && data[0] == 0x0d) return "CodeId1Mappings_Codabar";
        if (data_size >= 1 && data[0] == 0x0e) return "CodeId1Mappings_Code93";
        if (data_size >= 1 && data[0] == 0x0f) return "CodeId1Mappings_Code11";
        if (data_size >= 1 && data[0] == 0x10) return "CodeId1Mappings_Msi_Plessey";
        if (data_size >= 1 && data[0] == 0x11) return "CodeId1Mappings_Rss14";
        if (data_size >= 1 && data[0] == 0x12) return "CodeId1Mappings_Rss14Limited";
        if (data_size >= 1 && data[0] == 0x13) return "CodeId1Mappings_Rss14Expanded";
        if (data_size >= 1 && data[0] == 0x14) return "CodeId1Mappings_Rss14Stacked";
        if (data_size >= 1 && data[0] == 0x15) return "CodeId1Mappings_Pdf417";
        if (data_size >= 1 && data[0] == 0x16) return "CodeId1Mappings_DataMatrix";
        if (data_size >= 1 && data[0] == 0x17) return "CodeId1Mappings_QRCode";
        if (data_size >= 1 && data[0] == 0x18) return "CodeId1Mappings_AztecCode";
        if (data_size >= 1 && data[0] == 0x19) return "CodeId1Mappings_Maxicode";
        if (data_size >= 1 && data[0] == 0x1a) return "CodeId1Mappings_MicroPdf417";
        if (data_size >= 1 && data[0] == 0x1b) return "CodeId1Mappings_MicroQR";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 116: {
        if (data_size >= 1 && data[0] == 0x00) return "Extra Light";
        if (data_size >= 1 && data[0] == 0x01) return "Light";
        if (data_size >= 1 && data[0] == 0x02) return "Normal";
        if (data_size >= 1 && data[0] == 0x03) return "Heavy";
        if (data_size >= 1 && data[0] == 0x04) return "Extra Heavy";
        if (data_size >= 1 && data[0] == 0x05) return "Invalid";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 117: {
        if (data_size >= 1 && data[0] == 0x00) return "Deprecated Tap Cold";
        if (data_size >= 1 && data[0] == 0x01) return "Deprecated Cold";
        if (data_size >= 1 && data[0] == 0x02) return "Deprecated Warm";
        if (data_size >= 1 && data[0] == 0x03) return "Deprecated Hot";
        if (data_size >= 1 && data[0] == 0x04) return "Deprecated Extra Hot";
        if (data_size >= 1 && data[0] == 0x06) return "Disable";
        if (data_size >= 1 && data[0] == 0x10) return "Tap Cold";
        if (data_size >= 1 && data[0] == 0x11) return "Cold";
        if (data_size >= 1 && data[0] == 0x12) return "Cool";
        if (data_size >= 1 && data[0] == 0x13) return "Colors";
        if (data_size >= 1 && data[0] == 0x14) return "Warm";
        if (data_size >= 1 && data[0] == 0x15) return "Hot";
        if (data_size >= 1 && data[0] == 0x16) return "Extra Hot";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 118: {
        if (data_size >= 1 && data[0] == 0x00) return "No Spin";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "Extra High";
        if (data_size >= 1 && data[0] == 0x05) return "Disable";
        if (data_size >= 1 && data[0] == 0x06) return "Max";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 119: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Deep Rinse (Fabric Softener)";
        if (data_size >= 1 && data[0] == 0x02) return "Extra Rinse";
        if (data_size >= 1 && data[0] == 0x03) return "Max (Deep Rinse + Extra Rinse)";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 120: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 121: {
        if (data_size >= 1 && data[0] == 0x01) return "Basket Clean";
        if (data_size >= 1 && data[0] == 0x02) return "Drain and Spin";
        if (data_size >= 1 && data[0] == 0x03) return "Quick Rinse";
        if (data_size >= 1 && data[0] == 0x04) return "Bulky Items";
        if (data_size >= 1 && data[0] == 0x05) return "Sanitize";
        if (data_size >= 1 && data[0] == 0x06) return "Towels";
        if (data_size >= 1 && data[0] == 0x08) return "Normal";
        if (data_size >= 1 && data[0] == 0x09) return "Whites";
        if (data_size >= 1 && data[0] == 0x0a) return "Dark Colors";
        if (data_size >= 1 && data[0] == 0x0b) return "Jeans";
        if (data_size >= 1 && data[0] == 0x0c) return "Hand Wash";
        if (data_size >= 1 && data[0] == 0x0d) return "Delicates";
        if (data_size >= 1 && data[0] == 0x0e) return "Speed Wash";
        if (data_size >= 1 && data[0] == 0x0f) return "Heavy Duty";
        if (data_size >= 1 && data[0] == 0x10) return "Allergen";
        if (data_size >= 1 && data[0] == 0x11) return "Power Clean";
        if (data_size >= 1 && data[0] == 0x12) return "Rinse and Spin";
        if (data_size >= 1 && data[0] == 0x13) return "Single Item Wash";
        if (data_size >= 1 && data[0] == 0x21) return "Soak";
        if (data_size >= 1 && data[0] == 0x22) return "Wool";
        if (data_size >= 1 && data[0] == 0x25) return "Spin";
        if (data_size >= 1 && data[0] == 0x26) return "Everyday";
        if (data_size >= 1 && data[0] == 0x27) return "Soft Toys";
        if (data_size >= 1 && data[0] == 0x28) return "Sneakers";
        if (data_size >= 1 && data[0] == 0x29) return "Synthetics";
        if (data_size >= 1 && data[0] == 0x2a) return "Silk";
        if (data_size >= 1 && data[0] == 0x2b) return "Denim";
        if (data_size >= 1 && data[0] == 0x2c) return "Drum Clean";
        if (data_size >= 1 && data[0] == 0x2d) return "Sheets";
        if (data_size >= 1 && data[0] == 0x2e) return "Quick 15";
        if (data_size >= 1 && data[0] == 0x2f) return "Quick 30";
        if (data_size >= 1 && data[0] == 0x30) return "Easy Iron";
        if (data_size >= 1 && data[0] == 0x31) return "Sports";
        if (data_size >= 1 && data[0] == 0x32) return "Eco 40-60";
        if (data_size >= 1 && data[0] == 0x33) return "20�C";
        if (data_size >= 1 && data[0] == 0x80) return "Cottons";
        if (data_size >= 1 && data[0] == 0x81) return "Easy Care";
        if (data_size >= 1 && data[0] == 0x82) return "Active Wear";
        if (data_size >= 1 && data[0] == 0x83) return "Timed Dry";
        if (data_size >= 1 && data[0] == 0x84) return "DeWrinkle";
        if (data_size >= 1 && data[0] == 0x85) return "Air Fluff";
        if (data_size >= 1 && data[0] == 0x86) return "Steam Refresh";
        if (data_size >= 1 && data[0] == 0x87) return "Steam Dewrinkle";
        if (data_size >= 1 && data[0] == 0x88) return "Speed Dry";
        if (data_size >= 1 && data[0] == 0x89) return "Mixed";
        if (data_size >= 1 && data[0] == 0x8a) return "Quick Dry";
        if (data_size >= 1 && data[0] == 0x8b) return "Casuals";
        if (data_size >= 1 && data[0] == 0x8c) return "Warm Up";
        if (data_size >= 1 && data[0] == 0x8d) return "Energy Saver";
        if (data_size >= 1 && data[0] == 0x8f) return "Rack Dry";
        if (data_size >= 1 && data[0] == 0x90) return "Baby Care";
        if (data_size >= 1 && data[0] == 0x97) return "Pre Iron";
        if (data_size >= 1 && data[0] == 0x98) return "Hygiene";
        if (data_size >= 1 && data[0] == 0x99) return "Cool Air";
        if (data_size >= 1 && data[0] == 0x9a) return "Outdoor";
        if (data_size >= 1 && data[0] == 0x9b) return "Ultra Delicate";
        if (data_size >= 1 && data[0] == 0x9e) return "Durable";
        if (data_size >= 1 && data[0] == 0x9f) return "Shoes";
        if (data_size >= 1 && data[0] == 0xa0) return "Shirts";
        if (data_size >= 1 && data[0] == 0xa1) return "Refresh";
        if (data_size >= 1 && data[0] == 0xa2) return "Freshen";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 122: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 123: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 124: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 125: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 126: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop command";
        if (data_size >= 1 && data[0] == 0xff) return "Request Processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 127: {
        if (data_size >= 1 && data[0] == 0x00) return "Start Extended Tumble command";
        if (data_size >= 1 && data[0] == 0xff) return "Request Processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 128: {
        if (data_size >= 1 && data[0] == 0x00) return "EcoDry disabled";
        if (data_size >= 1 && data[0] == 0x01) return "EcoDry enabled";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 129: {
        if (data_size >= 1 && data[0] == 0x00) return "Damp Alert disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Damp Alert enabled";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 130: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Damp";
        if (data_size >= 1 && data[0] == 0x02) return "Less Dry";
        if (data_size >= 1 && data[0] == 0x03) return "Dry";
        if (data_size >= 1 && data[0] == 0x04) return "More Dry";
        if (data_size >= 1 && data[0] == 0x05) return "Extra Dry";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 131: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Damp";
        if (data_size >= 1 && data[0] == 0x02) return "Less Dry";
        if (data_size >= 1 && data[0] == 0x03) return "Dry";
        if (data_size >= 1 && data[0] == 0x04) return "More Dry";
        if (data_size >= 1 && data[0] == 0x05) return "Extra Dry";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 132: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "No Heat";
        if (data_size >= 1 && data[0] == 0x02) return "Extra Low";
        if (data_size >= 1 && data[0] == 0x03) return "Low";
        if (data_size >= 1 && data[0] == 0x04) return "Medium";
        if (data_size >= 1 && data[0] == 0x05) return "High";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 133: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "No Heat";
        if (data_size >= 1 && data[0] == 0x02) return "Extra Low";
        if (data_size >= 1 && data[0] == 0x03) return "Low";
        if (data_size >= 1 && data[0] == 0x04) return "Medium";
        if (data_size >= 1 && data[0] == 0x05) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 134: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 135: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 136: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 137: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 138: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 139: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 140: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 141: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 142: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 143: {
        if (data_size >= 1 && data[0] == 0x00) return "Request Processed";
        if (data_size >= 1 && data[0] == 0xff) return "Load Cycle";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 144: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 145: {
        if (data_size >= 1 && data[0] == 0x00) return "Flow Rate Bucket One";
        if (data_size >= 1 && data[0] == 0x01) return "Flow Rate Bucket Two";
        if (data_size >= 1 && data[0] == 0x02) return "Flow Rate Bucket Three";
        if (data_size >= 1 && data[0] == 0x03) return "Flow Rate Bucket Four";
        if (data_size >= 1 && data[0] == 0x04) return "Flow Rate Bucket Five";
        if (data_size >= 1 && data[0] == 0x05) return "Flow Rate Bucket Six";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 146: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 147: {
        if (data_size >= 1 && data[0] == 0x00) return "Time level option disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Time level option 1";
        if (data_size >= 1 && data[0] == 0x02) return "Time level option 2";
        if (data_size >= 1 && data[0] == 0x03) return "Time level option 3";
        if (data_size >= 1 && data[0] == 0x04) return "Time level option 4";
        if (data_size >= 1 && data[0] == 0x05) return "Time level option 5";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 148: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 149: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "High Extendend Alert";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 150: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "High Extended Alert";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 151: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Unlocked";
        if (data_size >= 1 && data[0] == 0x02) return "Locked";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 152: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 153: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 154: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "SteamRefresh";
        if (data_size >= 1 && data[0] == 0x02) return "SteamDewrinkle";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 155: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 156: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 157: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 158: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "0 Power 4 Care";
        if (data_size >= 1 && data[0] == 0x02) return "1 Power 3 Care";
        if (data_size >= 1 && data[0] == 0x03) return "2 Power 2 Care";
        if (data_size >= 1 && data[0] == 0x04) return "3 Power 1 Care";
        if (data_size >= 1 && data[0] == 0x05) return "4 Power 0 Care";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 159: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Auto";
        if (data_size >= 1 && data[0] == 0x02) return "More";
        if (data_size >= 1 && data[0] == 0x03) return "Less";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 160: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 161: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 162: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 163: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 164: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Defined";
        if (data_size >= 1 && data[0] == 0x01) return "Basket Clean";
        if (data_size >= 1 && data[0] == 0x02) return "Drain and Spin";
        if (data_size >= 1 && data[0] == 0x03) return "Quick Rinse";
        if (data_size >= 1 && data[0] == 0x04) return "Bulky Items";
        if (data_size >= 1 && data[0] == 0x05) return "Sanitize";
        if (data_size >= 1 && data[0] == 0x06) return "Towels or Sheets";
        if (data_size >= 1 && data[0] == 0x07) return "Washer Steam Refresh";
        if (data_size >= 1 && data[0] == 0x08) return "Normal or Mixed Load";
        if (data_size >= 1 && data[0] == 0x09) return "Whites";
        if (data_size >= 1 && data[0] == 0x0a) return "Dark Colors";
        if (data_size >= 1 && data[0] == 0x0b) return "Jeans";
        if (data_size >= 1 && data[0] == 0x0c) return "Hand Wash";
        if (data_size >= 1 && data[0] == 0x0d) return "Delicates";
        if (data_size >= 1 && data[0] == 0x0e) return "Speed Wash";
        if (data_size >= 1 && data[0] == 0x0f) return "Heavy Duty";
        if (data_size >= 1 && data[0] == 0x10) return "Allergen";
        if (data_size >= 1 && data[0] == 0x11) return "Power Clean";
        if (data_size >= 1 && data[0] == 0x12) return "Rinse and Spin";
        if (data_size >= 1 && data[0] == 0x13) return "Single Item Wash";
        if (data_size >= 1 && data[0] == 0x14) return "Colors";
        if (data_size >= 1 && data[0] == 0x15) return "Cold Wash";
        if (data_size >= 1 && data[0] == 0x16) return "Water Station";
        if (data_size >= 1 && data[0] == 0x17) return "Tub Clean";
        if (data_size >= 1 && data[0] == 0x18) return "Casuals with Steam";
        if (data_size >= 1 && data[0] == 0x19) return "Stain Wash with Steam";
        if (data_size >= 1 && data[0] == 0x1a) return "Deep Clean";
        if (data_size >= 1 && data[0] == 0x1b) return "Bulky Bedding";
        if (data_size >= 1 && data[0] == 0x1c) return "Normal";
        if (data_size >= 1 && data[0] == 0x1d) return "Quick Wash";
        if (data_size >= 1 && data[0] == 0x1e) return "Sanitize with Oxi";
        if (data_size >= 1 && data[0] == 0x1f) return "Self Clean";
        if (data_size >= 1 && data[0] == 0x20) return "Towels";
        if (data_size >= 1 && data[0] == 0x21) return "Soak";
        if (data_size >= 1 && data[0] == 0x22) return "Wool";
        if (data_size >= 1 && data[0] == 0x23) return "Ultra Fresh Vent";
        if (data_size >= 1 && data[0] == 0x24) return "Sanitize + Allergen";
        if (data_size >= 1 && data[0] == 0x25) return "Spin Only";
        if (data_size >= 1 && data[0] == 0x26) return "Everyday";
        if (data_size >= 1 && data[0] == 0x27) return "Soft Toys";
        if (data_size >= 1 && data[0] == 0x28) return "Sneakers";
        if (data_size >= 1 && data[0] == 0x29) return "Synthetics";
        if (data_size >= 1 && data[0] == 0x2a) return "Silk";
        if (data_size >= 1 && data[0] == 0x2b) return "Denim";
        if (data_size >= 1 && data[0] == 0x2c) return "Drum Clean";
        if (data_size >= 1 && data[0] == 0x2d) return "Sheets";
        if (data_size >= 1 && data[0] == 0x2e) return "Quick 15";
        if (data_size >= 1 && data[0] == 0x2f) return "Quick 30";
        if (data_size >= 1 && data[0] == 0x30) return "Easy Iron";
        if (data_size >= 1 && data[0] == 0x31) return "Sports";
        if (data_size >= 1 && data[0] == 0x32) return "Eco 40-60";
        if (data_size >= 1 && data[0] == 0x33) return "20 C";
        if (data_size >= 1 && data[0] == 0x34) return "Warm Wash";
        if (data_size >= 1 && data[0] == 0x35) return "Hot Wash";
        if (data_size >= 1 && data[0] == 0x36) return "Swim Wear";
        if (data_size >= 1 && data[0] == 0x37) return "Eco";
        if (data_size >= 1 && data[0] == 0x38) return "Express";
        if (data_size >= 1 && data[0] == 0x39) return "Mix";
        if (data_size >= 1 && data[0] == 0x3a) return "Quick Cycle";
        if (data_size >= 1 && data[0] == 0x3b) return "Duvet";
        if (data_size >= 1 && data[0] == 0x3c) return "Drum Dry";
        if (data_size >= 1 && data[0] == 0x80) return "Cottons";
        if (data_size >= 1 && data[0] == 0x81) return "Easy Care";
        if (data_size >= 1 && data[0] == 0x82) return "Active Wear";
        if (data_size >= 1 && data[0] == 0x83) return "Timed Dry";
        if (data_size >= 1 && data[0] == 0x84) return "Dewrinkle";
        if (data_size >= 1 && data[0] == 0x85) return "Quick or Air Fluff";
        if (data_size >= 1 && data[0] == 0x86) return "Steam Refresh";
        if (data_size >= 1 && data[0] == 0x87) return "Steam Dewrinkle";
        if (data_size >= 1 && data[0] == 0x88) return "Speed Dry";
        if (data_size >= 1 && data[0] == 0x89) return "Mixed";
        if (data_size >= 1 && data[0] == 0x8a) return "Quick Dry";
        if (data_size >= 1 && data[0] == 0x8b) return "Casuals";
        if (data_size >= 1 && data[0] == 0x8c) return "Warm Up";
        if (data_size >= 1 && data[0] == 0x8d) return "Energy Saver";
        if (data_size >= 1 && data[0] == 0x8e) return "Antibacterial";
        if (data_size >= 1 && data[0] == 0x8f) return "Rack Dry";
        if (data_size >= 1 && data[0] == 0x90) return "Baby Care";
        if (data_size >= 1 && data[0] == 0x91) return "Auto Dry";
        if (data_size >= 1 && data[0] == 0x92) return "Auto Extra Dry";
        if (data_size >= 1 && data[0] == 0x93) return "Perm Press";
        if (data_size >= 1 && data[0] == 0x94) return "Washer Link";
        if (data_size >= 1 && data[0] == 0x95) return "Auto Damp Dry";
        if (data_size >= 1 && data[0] == 0x96) return "Smart Vent";
        if (data_size >= 1 && data[0] == 0x97) return "Pre Iron";
        if (data_size >= 1 && data[0] == 0x98) return "Hygiene";
        if (data_size >= 1 && data[0] == 0x99) return "Cool Air";
        if (data_size >= 1 && data[0] == 0x9a) return "Outdoor";
        if (data_size >= 1 && data[0] == 0x9b) return "Ultra Delicate";
        if (data_size >= 1 && data[0] == 0x9c) return "Scent";
        if (data_size >= 1 && data[0] == 0x9d) return "Sanitize Steam";
        if (data_size >= 1 && data[0] == 0x9e) return "Durable";
        if (data_size >= 1 && data[0] == 0x9f) return "Shoes";
        if (data_size >= 1 && data[0] == 0xa0) return "Shirts";
        if (data_size >= 1 && data[0] == 0xa1) return "Refresh";
        if (data_size >= 1 && data[0] == 0xa2) return "Freshen";
        if (data_size >= 1 && data[0] == 0xa3) return "Eco Cool";
        if (data_size >= 1 && data[0] == 0xa4) return "Rinse & Dry";
        if (data_size >= 1 && data[0] == 0xa5) return "Leather";
        if (data_size >= 1 && data[0] == 0xa6) return "Outerwear";
        if (data_size >= 1 && data[0] == 0xa7) return "Mixed Refresh";
        if (data_size >= 1 && data[0] == 0xa8) return "Shirts Refresh";
        if (data_size >= 1 && data[0] == 0xa9) return "Delicate Refresh";
        if (data_size >= 1 && data[0] == 0xaa) return "Sanitise Refresh";
        if (data_size >= 1 && data[0] == 0xab) return "Light";
        if (data_size >= 1 && data[0] == 0xac) return "Heavy";
        if (data_size >= 1 && data[0] == 0xad) return "Wool or Knit";
        if (data_size >= 1 && data[0] == 0xae) return "Rain or Snow";
        if (data_size >= 1 && data[0] == 0xaf) return "Kids Item";
        if (data_size >= 1 && data[0] == 0xb0) return "Suits or Coats";
        if (data_size >= 1 && data[0] == 0xb1) return "Pants Crease";
        if (data_size >= 1 && data[0] == 0xb2) return "Steam Normal";
        if (data_size >= 1 && data[0] == 0xb3) return "Steam Whites";
        if (data_size >= 1 && data[0] == 0xb4) return "Steam Towels";
        if (data_size >= 1 && data[0] == 0xb5) return "Steam Sanitize";
        if (data_size >= 1 && data[0] == 0xb6) return "Down";
        if (data_size >= 1 && data[0] == 0xb7) return "Night Dry";
        if (data_size >= 1 && data[0] == 0xb8) return "QuietWash";
        if (data_size >= 1 && data[0] == 0xb9) return "Blocked Vent";
        if (data_size >= 1 && data[0] == 0xba) return "King Size Comforter";
        if (data_size >= 1 && data[0] == 0xbb) return "Kids Wear";
        if (data_size >= 1 && data[0] == 0xbc) return "Sweat Stains";
        if (data_size >= 1 && data[0] == 0xbd) return "Drain, Spin and Dry";
        if (data_size >= 1 && data[0] == 0xbe) return "UV Gentle Sanitize";
        if (data_size >= 1 && data[0] == 0xbf) return "UV Fast Sanitize";
        if (data_size >= 1 && data[0] == 0xfe) return "RemoteSelection";
        if (data_size >= 1 && data[0] == 0xff) return "Reset";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 165: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 166: {
        if (data_size >= 1 && data[0] == 0x00) return "AutoSoakLevel_0_Off";
        if (data_size >= 1 && data[0] == 0x01) return "AutoSoakLevel_1";
        if (data_size >= 1 && data[0] == 0x02) return "AutoSoakLevel_2";
        if (data_size >= 1 && data[0] == 0x03) return "AutoSoakLevel_3";
        if (data_size >= 1 && data[0] == 0x04) return "AutoSoakLevel_4";
        if (data_size >= 1 && data[0] == 0x05) return "Soak 30min";
        if (data_size >= 1 && data[0] == 0x06) return "Soak 60min";
        if (data_size >= 1 && data[0] == 0x07) return "Soak 90min";
        if (data_size >= 1 && data[0] == 0x08) return "Soak 120min";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 167: {
        if (data_size >= 1 && data[0] == 0x00) return "DeepFillIncremental_Off";
        if (data_size >= 1 && data[0] == 0x01) return "DeepFillIncremental_Incremental";
        if (data_size >= 1 && data[0] == 0x02) return "DeepFillIncremental_MaxFill";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 168: {
        if (data_size >= 1 && data[0] == 0x00) return "DeepFillFixedLevel_Disable";
        if (data_size >= 1 && data[0] == 0x01) return "DeepFillFixedLevel_Off";
        if (data_size >= 1 && data[0] == 0x02) return "DeepFillFixedLevel_1";
        if (data_size >= 1 && data[0] == 0x03) return "DeepFillFixedLevel_2";
        if (data_size >= 1 && data[0] == 0x04) return "DeepFillFixedLevel_MaxFill";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 169: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 170: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 171: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 172: {
        if (data_size >= 1 && data[0] == 0x00) return "SpinLevel_Disable";
        if (data_size >= 1 && data[0] == 0x01) return "SpinLevel_NoSpin";
        if (data_size >= 1 && data[0] == 0x02) return "SpinLevel_Low";
        if (data_size >= 1 && data[0] == 0x03) return "SpinLevel_Normal";
        if (data_size >= 1 && data[0] == 0x04) return "SpinLevel_More";
        if (data_size >= 1 && data[0] == 0x05) return "SpinLevel_Extra";
        if (data_size >= 1 && data[0] == 0x06) return "SpinLevel_Max";
        if (data_size >= 1 && data[0] == 0x07) return "SpinLevel_MaxLoadSize";
        if (data_size >= 1 && data[0] == 0x08) return "SpinLevel_NonSheddingLoads";
        if (data_size >= 1 && data[0] == 0x09) return "Med";
        if (data_size >= 1 && data[0] == 0x0a) return "High";
        if (data_size >= 1 && data[0] == 0x0b) return "400";
        if (data_size >= 1 && data[0] == 0x0c) return "500";
        if (data_size >= 1 && data[0] == 0x0d) return "600";
        if (data_size >= 1 && data[0] == 0x0e) return "800";
        if (data_size >= 1 && data[0] == 0x0f) return "1000";
        if (data_size >= 1 && data[0] == 0x10) return "1100";
        if (data_size >= 1 && data[0] == 0x11) return "1200";
        if (data_size >= 1 && data[0] == 0x12) return "1400";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 173: {
        if (data_size >= 1 && data[0] == 0x00) return "WaterOnDemandSoapDispenseOption_Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "WaterOnDemandSoapDispenseOption_SoapyWater";
        if (data_size >= 1 && data[0] == 0x02) return "WaterOnDemandSoapDispenseOption_WaterOnly";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 174: {
        if (data_size >= 1 && data[0] == 0x00) return "WaterTemp_Disable";
        if (data_size >= 1 && data[0] == 0x01) return "WaterTemp_Tapcold";
        if (data_size >= 1 && data[0] == 0x02) return "WaterTemp_Cold";
        if (data_size >= 1 && data[0] == 0x03) return "WaterTemp_Cool";
        if (data_size >= 1 && data[0] == 0x04) return "WaterTemp_Colors";
        if (data_size >= 1 && data[0] == 0x05) return "WaterTemp_Warm";
        if (data_size >= 1 && data[0] == 0x06) return "WaterTemp_Hot";
        if (data_size >= 1 && data[0] == 0x07) return "WaterTemp_ExtraHot";
        if (data_size >= 1 && data[0] == 0x08) return "20°C";
        if (data_size >= 1 && data[0] == 0x09) return "30°C";
        if (data_size >= 1 && data[0] == 0x0a) return "40°C";
        if (data_size >= 1 && data[0] == 0x0b) return "50°C";
        if (data_size >= 1 && data[0] == 0x0c) return "60°C";
        if (data_size >= 1 && data[0] == 0x0d) return "70°C";
        if (data_size >= 1 && data[0] == 0x0e) return "80°C";
        if (data_size >= 1 && data[0] == 0x0f) return "90°C";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 175: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 176: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Extra Light";
        if (data_size >= 1 && data[0] == 0x02) return "Light";
        if (data_size >= 1 && data[0] == 0x03) return "Normal";
        if (data_size >= 1 && data[0] == 0x04) return "Heavy";
        if (data_size >= 1 && data[0] == 0x05) return "Extra Heavy";
        if (data_size >= 1 && data[0] == 0x06) return "Auto";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 177: {
        if (data_size >= 1 && data[0] == 0x00) return "Detergent";
        if (data_size >= 1 && data[0] == 0x01) return "Softener";
        if (data_size >= 1 && data[0] == 0x02) return "Fabric Rinse";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 178: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 179: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 180: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Protein Based";
        if (data_size >= 1 && data[0] == 0x02) return "Tannin Based";
        if (data_size >= 1 && data[0] == 0x03) return "Soil Based";
        if (data_size >= 1 && data[0] == 0x04) return "Acid Based";
        if (data_size >= 1 && data[0] == 0x05) return "Beverage";
        if (data_size >= 1 && data[0] == 0x06) return "Oil Based";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 181: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Defined";
        if (data_size >= 1 && data[0] == 0x01) return "Basket Clean";
        if (data_size >= 1 && data[0] == 0x02) return "Drain and Spin";
        if (data_size >= 1 && data[0] == 0x03) return "Quick Rinse";
        if (data_size >= 1 && data[0] == 0x04) return "Bulky Items";
        if (data_size >= 1 && data[0] == 0x05) return "Sanitize";
        if (data_size >= 1 && data[0] == 0x06) return "Towels or Sheets";
        if (data_size >= 1 && data[0] == 0x07) return "Washer Steam Refresh";
        if (data_size >= 1 && data[0] == 0x08) return "Normal or Mixed Load";
        if (data_size >= 1 && data[0] == 0x09) return "Whites";
        if (data_size >= 1 && data[0] == 0x0a) return "Dark Colors";
        if (data_size >= 1 && data[0] == 0x0b) return "Jeans";
        if (data_size >= 1 && data[0] == 0x0c) return "Hand Wash";
        if (data_size >= 1 && data[0] == 0x0d) return "Delicates";
        if (data_size >= 1 && data[0] == 0x0e) return "Speed Wash";
        if (data_size >= 1 && data[0] == 0x0f) return "Heavy Duty";
        if (data_size >= 1 && data[0] == 0x10) return "Allergen";
        if (data_size >= 1 && data[0] == 0x11) return "Power Clean";
        if (data_size >= 1 && data[0] == 0x12) return "Rinse and Spin";
        if (data_size >= 1 && data[0] == 0x13) return "Single Item Wash";
        if (data_size >= 1 && data[0] == 0x14) return "Colors";
        if (data_size >= 1 && data[0] == 0x15) return "Cold Wash";
        if (data_size >= 1 && data[0] == 0x16) return "Water Station";
        if (data_size >= 1 && data[0] == 0x17) return "Tub Clean";
        if (data_size >= 1 && data[0] == 0x18) return "Casuals with Steam";
        if (data_size >= 1 && data[0] == 0x19) return "Stain Wash with Steam";
        if (data_size >= 1 && data[0] == 0x1a) return "Deep Clean";
        if (data_size >= 1 && data[0] == 0x1b) return "Bulky Bedding";
        if (data_size >= 1 && data[0] == 0x1c) return "Normal";
        if (data_size >= 1 && data[0] == 0x1d) return "Quick Wash";
        if (data_size >= 1 && data[0] == 0x1e) return "Sanitize with Oxi";
        if (data_size >= 1 && data[0] == 0x1f) return "Self Clean";
        if (data_size >= 1 && data[0] == 0x20) return "Towels";
        if (data_size >= 1 && data[0] == 0x21) return "Soak";
        if (data_size >= 1 && data[0] == 0x22) return "Wool";
        if (data_size >= 1 && data[0] == 0x23) return "Ultra Fresh Vent";
        if (data_size >= 1 && data[0] == 0x24) return "Sanitize + Allergen";
        if (data_size >= 1 && data[0] == 0x25) return "Spin Only";
        if (data_size >= 1 && data[0] == 0x26) return "Everyday";
        if (data_size >= 1 && data[0] == 0x27) return "Soft Toys";
        if (data_size >= 1 && data[0] == 0x28) return "Sneakers";
        if (data_size >= 1 && data[0] == 0x29) return "Synthetics";
        if (data_size >= 1 && data[0] == 0x2a) return "Silk";
        if (data_size >= 1 && data[0] == 0x2b) return "Denim";
        if (data_size >= 1 && data[0] == 0x2c) return "Drum Clean";
        if (data_size >= 1 && data[0] == 0x2d) return "Sheets";
        if (data_size >= 1 && data[0] == 0x2e) return "Quick 15";
        if (data_size >= 1 && data[0] == 0x2f) return "Quick 30";
        if (data_size >= 1 && data[0] == 0x30) return "Easy Iron";
        if (data_size >= 1 && data[0] == 0x31) return "Sports";
        if (data_size >= 1 && data[0] == 0x32) return "Eco 40-60";
        if (data_size >= 1 && data[0] == 0x33) return "20 C";
        if (data_size >= 1 && data[0] == 0x34) return "Warm Wash";
        if (data_size >= 1 && data[0] == 0x35) return "Hot Wash";
        if (data_size >= 1 && data[0] == 0x36) return "Swim Wear";
        if (data_size >= 1 && data[0] == 0x37) return "Eco";
        if (data_size >= 1 && data[0] == 0x38) return "Express";
        if (data_size >= 1 && data[0] == 0x39) return "Mix";
        if (data_size >= 1 && data[0] == 0x3a) return "Quick Cycle";
        if (data_size >= 1 && data[0] == 0x3b) return "Duvet";
        if (data_size >= 1 && data[0] == 0x3c) return "Drum Dry";
        if (data_size >= 1 && data[0] == 0x80) return "Cottons";
        if (data_size >= 1 && data[0] == 0x81) return "Easy Care";
        if (data_size >= 1 && data[0] == 0x82) return "Active Wear";
        if (data_size >= 1 && data[0] == 0x83) return "Timed Dry";
        if (data_size >= 1 && data[0] == 0x84) return "Dewrinkle";
        if (data_size >= 1 && data[0] == 0x85) return "Quick or Air Fluff";
        if (data_size >= 1 && data[0] == 0x86) return "Steam Refresh";
        if (data_size >= 1 && data[0] == 0x87) return "Steam Dewrinkle";
        if (data_size >= 1 && data[0] == 0x88) return "Speed Dry";
        if (data_size >= 1 && data[0] == 0x89) return "Mixed";
        if (data_size >= 1 && data[0] == 0x8a) return "Quick Dry";
        if (data_size >= 1 && data[0] == 0x8b) return "Casuals";
        if (data_size >= 1 && data[0] == 0x8c) return "Warm Up";
        if (data_size >= 1 && data[0] == 0x8d) return "Energy Saver";
        if (data_size >= 1 && data[0] == 0x8e) return "Antibacterial";
        if (data_size >= 1 && data[0] == 0x8f) return "Rack Dry";
        if (data_size >= 1 && data[0] == 0x90) return "Baby Care";
        if (data_size >= 1 && data[0] == 0x91) return "Auto Dry";
        if (data_size >= 1 && data[0] == 0x92) return "Auto Extra Dry";
        if (data_size >= 1 && data[0] == 0x93) return "Perm Press";
        if (data_size >= 1 && data[0] == 0x94) return "Washer Link";
        if (data_size >= 1 && data[0] == 0x95) return "Auto Damp Dry";
        if (data_size >= 1 && data[0] == 0x96) return "Smart Vent";
        if (data_size >= 1 && data[0] == 0x97) return "Pre Iron";
        if (data_size >= 1 && data[0] == 0x98) return "Hygiene";
        if (data_size >= 1 && data[0] == 0x99) return "Cool Air";
        if (data_size >= 1 && data[0] == 0x9a) return "Outdoor";
        if (data_size >= 1 && data[0] == 0x9b) return "Ultra Delicate";
        if (data_size >= 1 && data[0] == 0x9c) return "Scent";
        if (data_size >= 1 && data[0] == 0x9d) return "Sanitize Steam";
        if (data_size >= 1 && data[0] == 0x9e) return "Durable";
        if (data_size >= 1 && data[0] == 0x9f) return "Shoes";
        if (data_size >= 1 && data[0] == 0xa0) return "Shirts";
        if (data_size >= 1 && data[0] == 0xa1) return "Refresh";
        if (data_size >= 1 && data[0] == 0xa2) return "Freshen";
        if (data_size >= 1 && data[0] == 0xa3) return "Eco Cool";
        if (data_size >= 1 && data[0] == 0xa4) return "Rinse & Dry";
        if (data_size >= 1 && data[0] == 0xa5) return "Leather";
        if (data_size >= 1 && data[0] == 0xa6) return "Outerwear";
        if (data_size >= 1 && data[0] == 0xa7) return "Mixed Refresh";
        if (data_size >= 1 && data[0] == 0xa8) return "Shirts Refresh";
        if (data_size >= 1 && data[0] == 0xa9) return "Delicate Refresh";
        if (data_size >= 1 && data[0] == 0xaa) return "Sanitise Refresh";
        if (data_size >= 1 && data[0] == 0xab) return "Light";
        if (data_size >= 1 && data[0] == 0xac) return "Heavy";
        if (data_size >= 1 && data[0] == 0xad) return "Wool or Knit";
        if (data_size >= 1 && data[0] == 0xae) return "Rain or Snow";
        if (data_size >= 1 && data[0] == 0xaf) return "Kids Item";
        if (data_size >= 1 && data[0] == 0xb0) return "Suits or Coats";
        if (data_size >= 1 && data[0] == 0xb1) return "Pants Crease";
        if (data_size >= 1 && data[0] == 0xb2) return "Steam Normal";
        if (data_size >= 1 && data[0] == 0xb3) return "Steam Whites";
        if (data_size >= 1 && data[0] == 0xb4) return "Steam Towels";
        if (data_size >= 1 && data[0] == 0xb5) return "Steam Sanitize";
        if (data_size >= 1 && data[0] == 0xb6) return "Down";
        if (data_size >= 1 && data[0] == 0xb7) return "Night Dry";
        if (data_size >= 1 && data[0] == 0xb8) return "QuietWash";
        if (data_size >= 1 && data[0] == 0xb9) return "Blocked Vent";
        if (data_size >= 1 && data[0] == 0xba) return "King Size Comforter";
        if (data_size >= 1 && data[0] == 0xbb) return "Kids Wear";
        if (data_size >= 1 && data[0] == 0xbc) return "Sweat Stains";
        if (data_size >= 1 && data[0] == 0xbd) return "Drain, Spin and Dry";
        if (data_size >= 1 && data[0] == 0xbe) return "UV Gentle Sanitize";
        if (data_size >= 1 && data[0] == 0xbf) return "UV Fast Sanitize";
        if (data_size >= 1 && data[0] == 0xfe) return "RemoteSelection";
        if (data_size >= 1 && data[0] == 0xff) return "Reset";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 182: {
        if (data_size >= 1 && data[0] == 0x00) return "None";
        if (data_size >= 1 && data[0] == 0x01) return "Pre Wash";
        if (data_size >= 1 && data[0] == 0x02) return "Main Wash";
        if (data_size >= 1 && data[0] == 0x03) return "Post Wash";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 183: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 184: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 185: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 186: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 187: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 188: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 189: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 190: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 191: {
        if (data_size >= 1 && data[0] == 0x00) return "No Spin";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "Extra High";
        if (data_size >= 1 && data[0] == 0x05) return "Disable";
        if (data_size >= 1 && data[0] == 0x06) return "Max";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 192: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 193: {
        if (data_size >= 1 && data[0] == 0x00) return "Start sensored dry only cycle command";
        if (data_size >= 1 && data[0] == 0xff) return "Request Processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 194: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 195: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 196: {
        if (data_size >= 1 && data[0] == 0x00) return "Flow Rate Bucket One";
        if (data_size >= 1 && data[0] == 0x01) return "Flow Rate Bucket Two";
        if (data_size >= 1 && data[0] == 0x02) return "Flow Rate Bucket Three";
        if (data_size >= 1 && data[0] == 0x03) return "Flow Rate Bucket Four";
        if (data_size >= 1 && data[0] == 0x04) return "Flow Rate Bucket Five";
        if (data_size >= 1 && data[0] == 0x05) return "Flow Rate Bucket Six";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 197: {
        if (data_size >= 1 && data[0] == 0x00) return "Detergent";
        if (data_size >= 1 && data[0] == 0x01) return "Softener";
        if (data_size >= 1 && data[0] == 0x02) return "Fabric Rinse";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 198: {
        if (data_size >= 1 && data[0] == 0x00) return "Start Remote Cycle command";
        if (data_size >= 1 && data[0] == 0xff) return "Request Processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 199: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 200: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 201: {
        if (data_size >= 1 && data[0] == 0x01) return "1 Item";
        if (data_size >= 1 && data[0] == 0x02) return "2 Items";
        if (data_size >= 1 && data[0] == 0x03) return "3 Items";
        if (data_size >= 1 && data[0] == 0x04) return "4 Items";
        if (data_size >= 1 && data[0] == 0x05) return "5 Items";
        if (data_size >= 1 && data[0] == 0x64) return "2 - 4 Items";
        if (data_size >= 1 && data[0] == 0x65) return "6 - 12 Items";
        if (data_size >= 1 && data[0] == 0xff) return "Request processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 202: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 203: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 204: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 205: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 206: {
        if (data_size >= 1 && data[0] == 0x01) return "Reset Coin Box Count";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 207: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 208: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 209: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 210: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 211: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop command";
        if (data_size >= 1 && data[0] == 0xff) return "Request Processed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 212: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Defined";
        if (data_size >= 1 && data[0] == 0x01) return "Basket Clean";
        if (data_size >= 1 && data[0] == 0x02) return "Drain and Spin";
        if (data_size >= 1 && data[0] == 0x03) return "Quick Rinse";
        if (data_size >= 1 && data[0] == 0x04) return "Bulky Items";
        if (data_size >= 1 && data[0] == 0x05) return "Sanitize";
        if (data_size >= 1 && data[0] == 0x06) return "Towels or Sheets";
        if (data_size >= 1 && data[0] == 0x07) return "Washer Steam Refresh";
        if (data_size >= 1 && data[0] == 0x08) return "Normal or Mixed Load";
        if (data_size >= 1 && data[0] == 0x09) return "Whites";
        if (data_size >= 1 && data[0] == 0x0a) return "Dark Colors";
        if (data_size >= 1 && data[0] == 0x0b) return "Jeans";
        if (data_size >= 1 && data[0] == 0x0c) return "Hand Wash";
        if (data_size >= 1 && data[0] == 0x0d) return "Delicates";
        if (data_size >= 1 && data[0] == 0x0e) return "Speed Wash";
        if (data_size >= 1 && data[0] == 0x0f) return "Heavy Duty";
        if (data_size >= 1 && data[0] == 0x10) return "Allergen";
        if (data_size >= 1 && data[0] == 0x11) return "Power Clean";
        if (data_size >= 1 && data[0] == 0x12) return "Rinse and Spin";
        if (data_size >= 1 && data[0] == 0x13) return "Single Item Wash";
        if (data_size >= 1 && data[0] == 0x14) return "Colors";
        if (data_size >= 1 && data[0] == 0x15) return "Cold Wash";
        if (data_size >= 1 && data[0] == 0x16) return "Water Station";
        if (data_size >= 1 && data[0] == 0x17) return "Tub Clean";
        if (data_size >= 1 && data[0] == 0x18) return "Casuals with Steam";
        if (data_size >= 1 && data[0] == 0x19) return "Stain Wash with Steam";
        if (data_size >= 1 && data[0] == 0x1a) return "Deep Clean";
        if (data_size >= 1 && data[0] == 0x1b) return "Bulky Bedding";
        if (data_size >= 1 && data[0] == 0x1c) return "Normal";
        if (data_size >= 1 && data[0] == 0x1d) return "Quick Wash";
        if (data_size >= 1 && data[0] == 0x1e) return "Sanitize with Oxi";
        if (data_size >= 1 && data[0] == 0x1f) return "Self Clean";
        if (data_size >= 1 && data[0] == 0x20) return "Towels";
        if (data_size >= 1 && data[0] == 0x21) return "Soak";
        if (data_size >= 1 && data[0] == 0x22) return "Wool";
        if (data_size >= 1 && data[0] == 0x23) return "Ultra Fresh Vent";
        if (data_size >= 1 && data[0] == 0x24) return "Sanitize + Allergen";
        if (data_size >= 1 && data[0] == 0x25) return "Spin Only";
        if (data_size >= 1 && data[0] == 0x26) return "Everyday";
        if (data_size >= 1 && data[0] == 0x27) return "Soft Toys";
        if (data_size >= 1 && data[0] == 0x28) return "Sneakers";
        if (data_size >= 1 && data[0] == 0x29) return "Synthetics";
        if (data_size >= 1 && data[0] == 0x2a) return "Silk";
        if (data_size >= 1 && data[0] == 0x2b) return "Denim";
        if (data_size >= 1 && data[0] == 0x2c) return "Drum Clean";
        if (data_size >= 1 && data[0] == 0x2d) return "Sheets";
        if (data_size >= 1 && data[0] == 0x2e) return "Quick 15";
        if (data_size >= 1 && data[0] == 0x2f) return "Quick 30";
        if (data_size >= 1 && data[0] == 0x30) return "Easy Iron";
        if (data_size >= 1 && data[0] == 0x31) return "Sports";
        if (data_size >= 1 && data[0] == 0x32) return "Eco 40-60";
        if (data_size >= 1 && data[0] == 0x33) return "20 C";
        if (data_size >= 1 && data[0] == 0x34) return "Warm Wash";
        if (data_size >= 1 && data[0] == 0x35) return "Hot Wash";
        if (data_size >= 1 && data[0] == 0x36) return "Swim Wear";
        if (data_size >= 1 && data[0] == 0x37) return "Eco";
        if (data_size >= 1 && data[0] == 0x38) return "Express";
        if (data_size >= 1 && data[0] == 0x39) return "Mix";
        if (data_size >= 1 && data[0] == 0x3a) return "Quick Cycle";
        if (data_size >= 1 && data[0] == 0x3b) return "Duvet";
        if (data_size >= 1 && data[0] == 0x3c) return "Drum Dry";
        if (data_size >= 1 && data[0] == 0x80) return "Cottons";
        if (data_size >= 1 && data[0] == 0x81) return "Easy Care";
        if (data_size >= 1 && data[0] == 0x82) return "Active Wear";
        if (data_size >= 1 && data[0] == 0x83) return "Timed Dry";
        if (data_size >= 1 && data[0] == 0x84) return "Dewrinkle";
        if (data_size >= 1 && data[0] == 0x85) return "Quick or Air Fluff";
        if (data_size >= 1 && data[0] == 0x86) return "Steam Refresh";
        if (data_size >= 1 && data[0] == 0x87) return "Steam Dewrinkle";
        if (data_size >= 1 && data[0] == 0x88) return "Speed Dry";
        if (data_size >= 1 && data[0] == 0x89) return "Mixed";
        if (data_size >= 1 && data[0] == 0x8a) return "Quick Dry";
        if (data_size >= 1 && data[0] == 0x8b) return "Casuals";
        if (data_size >= 1 && data[0] == 0x8c) return "Warm Up";
        if (data_size >= 1 && data[0] == 0x8d) return "Energy Saver";
        if (data_size >= 1 && data[0] == 0x8e) return "Antibacterial";
        if (data_size >= 1 && data[0] == 0x8f) return "Rack Dry";
        if (data_size >= 1 && data[0] == 0x90) return "Baby Care";
        if (data_size >= 1 && data[0] == 0x91) return "Auto Dry";
        if (data_size >= 1 && data[0] == 0x92) return "Auto Extra Dry";
        if (data_size >= 1 && data[0] == 0x93) return "Perm Press";
        if (data_size >= 1 && data[0] == 0x94) return "Washer Link";
        if (data_size >= 1 && data[0] == 0x95) return "Auto Damp Dry";
        if (data_size >= 1 && data[0] == 0x96) return "Smart Vent";
        if (data_size >= 1 && data[0] == 0x97) return "Pre Iron";
        if (data_size >= 1 && data[0] == 0x98) return "Hygiene";
        if (data_size >= 1 && data[0] == 0x99) return "Cool Air";
        if (data_size >= 1 && data[0] == 0x9a) return "Outdoor";
        if (data_size >= 1 && data[0] == 0x9b) return "Ultra Delicate";
        if (data_size >= 1 && data[0] == 0x9c) return "Scent";
        if (data_size >= 1 && data[0] == 0x9d) return "Sanitize Steam";
        if (data_size >= 1 && data[0] == 0x9e) return "Durable";
        if (data_size >= 1 && data[0] == 0x9f) return "Shoes";
        if (data_size >= 1 && data[0] == 0xa0) return "Shirts";
        if (data_size >= 1 && data[0] == 0xa1) return "Refresh";
        if (data_size >= 1 && data[0] == 0xa2) return "Freshen";
        if (data_size >= 1 && data[0] == 0xa3) return "Eco Cool";
        if (data_size >= 1 && data[0] == 0xa4) return "Rinse & Dry";
        if (data_size >= 1 && data[0] == 0xa5) return "Leather";
        if (data_size >= 1 && data[0] == 0xa6) return "Outerwear";
        if (data_size >= 1 && data[0] == 0xa7) return "Mixed Refresh";
        if (data_size >= 1 && data[0] == 0xa8) return "Shirts Refresh";
        if (data_size >= 1 && data[0] == 0xa9) return "Delicate Refresh";
        if (data_size >= 1 && data[0] == 0xaa) return "Sanitise Refresh";
        if (data_size >= 1 && data[0] == 0xab) return "Light";
        if (data_size >= 1 && data[0] == 0xac) return "Heavy";
        if (data_size >= 1 && data[0] == 0xad) return "Wool or Knit";
        if (data_size >= 1 && data[0] == 0xae) return "Rain or Snow";
        if (data_size >= 1 && data[0] == 0xaf) return "Kids Item";
        if (data_size >= 1 && data[0] == 0xb0) return "Suits or Coats";
        if (data_size >= 1 && data[0] == 0xb1) return "Pants Crease";
        if (data_size >= 1 && data[0] == 0xb2) return "Steam Normal";
        if (data_size >= 1 && data[0] == 0xb3) return "Steam Whites";
        if (data_size >= 1 && data[0] == 0xb4) return "Steam Towels";
        if (data_size >= 1 && data[0] == 0xb5) return "Steam Sanitize";
        if (data_size >= 1 && data[0] == 0xb6) return "Down";
        if (data_size >= 1 && data[0] == 0xb7) return "Night Dry";
        if (data_size >= 1 && data[0] == 0xb8) return "QuietWash";
        if (data_size >= 1 && data[0] == 0xb9) return "Blocked Vent";
        if (data_size >= 1 && data[0] == 0xba) return "King Size Comforter";
        if (data_size >= 1 && data[0] == 0xbb) return "Kids Wear";
        if (data_size >= 1 && data[0] == 0xbc) return "Sweat Stains";
        if (data_size >= 1 && data[0] == 0xbd) return "Drain, Spin and Dry";
        if (data_size >= 1 && data[0] == 0xbe) return "UV Gentle Sanitize";
        if (data_size >= 1 && data[0] == 0xbf) return "UV Fast Sanitize";
        if (data_size >= 1 && data[0] == 0xfe) return "RemoteSelection";
        if (data_size >= 1 && data[0] == 0xff) return "Reset";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 213: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Set";
        if (data_size >= 1 && data[0] == 0x01) return "Detergent 1";
        if (data_size >= 1 && data[0] == 0x02) return "Detergent 2";
        if (data_size >= 1 && data[0] == 0x03) return "Softener";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 214: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Set";
        if (data_size >= 1 && data[0] == 0x01) return "Detergent 1";
        if (data_size >= 1 && data[0] == 0x02) return "Detergent 2";
        if (data_size >= 1 && data[0] == 0x03) return "Softener";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 215: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "15 Minutes";
        if (data_size >= 1 && data[0] == 0x02) return "30 Minutes";
        if (data_size >= 1 && data[0] == 0x03) return "45 Minutes";
        if (data_size >= 1 && data[0] == 0x04) return "60 Minutes";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 216: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 217: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 218: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 219: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 220: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 221: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 222: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 223: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 224: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 225: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 226: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 227: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 228: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 229: {
        if (data_size >= 1 && data[0] == 0x00) return "PaidMode";
        if (data_size >= 1 && data[0] == 0x01) return "FreeModeWithNoApp";
        if (data_size >= 1 && data[0] == 0x02) return "FreeModeWithApp";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 230: {
        if (data_size >= 1 && data[0] == 0x00) return "Auto Sel";
        if (data_size >= 1 && data[0] == 0x01) return "Aqua Saver Sel";
        if (data_size >= 1 && data[0] == 0x02) return "Minimum Sel";
        if (data_size >= 1 && data[0] == 0x03) return "Low Sel";
        if (data_size >= 1 && data[0] == 0x04) return "Mid Sel";
        if (data_size >= 1 && data[0] == 0x05) return "High Sel";
        if (data_size >= 1 && data[0] == 0x06) return "Maximum Sel";
        if (data_size >= 1 && data[0] == 0x07) return "Maximum Iec Sel";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 231: {
        if (data_size >= 1 && data[0] == 0x00) return "STANDARD_SEL";
        if (data_size >= 1 && data[0] == 0x01) return "SOAK_30_MIN_SEL";
        if (data_size >= 1 && data[0] == 0x02) return "SOAK_60_MIN_SEL";
        if (data_size >= 1 && data[0] == 0x03) return "ONLY_WASH_SEL";
        if (data_size >= 1 && data[0] == 0x04) return "ONLY_DEEP_RINSE_SEL";
        if (data_size >= 1 && data[0] == 0x05) return "WASH_RINSE_SEL";
        if (data_size >= 1 && data[0] == 0x06) return "WASH_SPIN_SEL";
        if (data_size >= 1 && data[0] == 0x07) return "RINSE_SPIN_SEL";
        if (data_size >= 1 && data[0] == 0x08) return "DRAIN_N_SPIN_SEL";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 232: {
        if (data_size >= 1 && data[0] == 0x00) return "Auto";
        if (data_size >= 1 && data[0] == 0x01) return "Min";
        if (data_size >= 1 && data[0] == 0x02) return "Low";
        if (data_size >= 1 && data[0] == 0x03) return "Mid";
        if (data_size >= 1 && data[0] == 0x04) return "High";
        if (data_size >= 1 && data[0] == 0x05) return "Max";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 233: {
        if (data_size >= 1 && data[0] == 0x00) return "Cold Extra";
        if (data_size >= 1 && data[0] == 0x01) return "Cold";
        if (data_size >= 1 && data[0] == 0x02) return "Cold Warm";
        if (data_size >= 1 && data[0] == 0x03) return "Hot Warm";
        if (data_size >= 1 && data[0] == 0x04) return "Hot";
        if (data_size >= 1 && data[0] == 0x05) return "Hot Extra";
        if (data_size >= 1 && data[0] == 0x06) return "Auto";
        if (data_size >= 1 && data[0] == 0x07) return "TapCold";
        if (data_size >= 1 && data[0] == 0x08) return "SelectionMax";
        if (data_size >= 1 && data[0] == 0x09) return "WaterTempMax";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 234: {
        if (data_size >= 1 && data[0] == 0x00) return "Spin Auto Sel";
        if (data_size >= 1 && data[0] == 0x01) return "Spin Minimum Sel";
        if (data_size >= 1 && data[0] == 0x02) return "Spin Mid Sel";
        if (data_size >= 1 && data[0] == 0x03) return "Spin High Sel";
        if (data_size >= 1 && data[0] == 0x04) return "Spin Maximum Sel";
        if (data_size >= 1 && data[0] == 0x05) return "Spin Normal";
        if (data_size >= 1 && data[0] == 0x06) return "Spin Pre Dry Sel";
        if (data_size >= 1 && data[0] == 0x07) return "Easy Iron Sel";
        if (data_size >= 1 && data[0] == 0x08) return "NoSpin";
        if (data_size >= 1 && data[0] == 0x09) return "MaxLoadSize";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 235: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 236: {
        if (data_size >= 1 && data[0] == 0x00) return "Idle";
        if (data_size >= 1 && data[0] == 0x01) return "Start Up";
        if (data_size >= 1 && data[0] == 0x02) return "Standby";
        if (data_size >= 1 && data[0] == 0x03) return "Operation Mode";
        if (data_size >= 1 && data[0] == 0x04) return "Pause";
        if (data_size >= 1 && data[0] == 0x05) return "End of Cycle";
        if (data_size >= 1 && data[0] == 0x06) return "Delay Start";
        if (data_size >= 1 && data[0] == 0x07) return "Critical Fault Mode";
        if (data_size >= 1 && data[0] == 0x08) return "Commissioning";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 237: {
        if (data_size >= 1 && data[0] == 0x00) return "Init";
        if (data_size >= 1 && data[0] == 0x01) return "Run App";
        if (data_size >= 1 && data[0] == 0x02) return "Test";
        if (data_size >= 1 && data[0] == 0x03) return "Service";
        if (data_size >= 1 && data[0] == 0x04) return "Fct";
        if (data_size >= 1 && data[0] == 0x05) return "Showroom";
        if (data_size >= 1 && data[0] == 0x06) return "Data Flash Write";
        if (data_size >= 1 && data[0] == 0x07) return "UI Test";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 238: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 239: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 240: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 241: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 242: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 243: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 244: {
        if (data_size >= 1 && data[0] == 0x00) return "No_Operation";
        if (data_size >= 1 && data[0] == 0x01) return "Start_Resume_Cycle";
        if (data_size >= 1 && data[0] == 0x02) return "Cancel_Cycle";
        if (data_size >= 1 && data[0] == 0x03) return "Pause_Cycle";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 245: {
        if (data_size >= 1 && data[0] == 0x00) return "Water softener off";
        if (data_size >= 1 && data[0] == 0x01) return "Water softener setting 1";
        if (data_size >= 1 && data[0] == 0x02) return "Water softener setting 2";
        if (data_size >= 1 && data[0] == 0x03) return "Water softener setting 3";
        if (data_size >= 1 && data[0] == 0x04) return "Water softener setting 4";
        if (data_size >= 1 && data[0] == 0x05) return "Water softener setting 5";
        if (data_size >= 1 && data[0] == 0x06) return "Water softener setting 6";
        if (data_size >= 1 && data[0] == 0x07) return "Water softener setting 7";
        if (data_size >= 1 && data[0] == 0x08) return "Water softener setting 8";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 246: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 247: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 248: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 249: {
        if (data_size >= 1 && data[0] == 0x00) return "eWashTemp_None";
        if (data_size >= 1 && data[0] == 0x01) return "eWashTemp_Boost";
        if (data_size >= 1 && data[0] == 0x02) return "eWashTemp_Sani";
        if (data_size >= 1 && data[0] == 0x03) return "eWashTemp_SaniAndBoost";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 250: {
        if (data_size >= 1 && data[0] == 0x00) return "eHeatedDry_None";
        if (data_size >= 1 && data[0] == 0x01) return "eHeatedDry_AddedHeat";
        if (data_size >= 1 && data[0] == 0x02) return "eHeatedDry_MaxDry";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 251: {
        if (data_size >= 1 && data[0] == 0x00) return "eWashZone_Both";
        if (data_size >= 1 && data[0] == 0x01) return "eWashZone_Lower";
        if (data_size >= 1 && data[0] == 0x02) return "eWashZone_Upper";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 252: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 253: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 254: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 255: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 256: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 257: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 258: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 259: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 260: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 261: {
        if (data_size >= 1 && data[0] == 0x00) return "No_Operation";
        if (data_size >= 1 && data[0] == 0x01) return "Start_Resume_Cycle";
        if (data_size >= 1 && data[0] == 0x02) return "Cancel_Cycle";
        if (data_size >= 1 && data[0] == 0x03) return "Pause_Cycle";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 262: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 263: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 264: {
        if (data_size >= 1 && data[0] == 0x00) return "eWashTemp_None";
        if (data_size >= 1 && data[0] == 0x01) return "eWashTemp_Boost";
        if (data_size >= 1 && data[0] == 0x02) return "eWashTemp_Sani";
        if (data_size >= 1 && data[0] == 0x03) return "eWashTemp_SaniAndBoost";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 265: {
        if (data_size >= 1 && data[0] == 0x00) return "eHeatedDry_None";
        if (data_size >= 1 && data[0] == 0x01) return "eHeatedDry_AddedHeat";
        if (data_size >= 1 && data[0] == 0x02) return "eHeatedDry_MaxDry";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 266: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 267: {
        if (data_size >= 1 && data[0] == 0x00) return "Hybrid";
        if (data_size >= 1 && data[0] == 0x01) return "Standard electric";
        if (data_size >= 1 && data[0] == 0x02) return "E-heat";
        if (data_size >= 1 && data[0] == 0x03) return "HiDemand";
        if (data_size >= 1 && data[0] == 0x04) return "Vacation";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 268: {
        if (data_size >= 1 && data[0] == 0x00) return "Hybrid";
        if (data_size >= 1 && data[0] == 0x01) return "Standard electric";
        if (data_size >= 1 && data[0] == 0x02) return "E-heat";
        if (data_size >= 1 && data[0] == 0x03) return "HiDemand";
        if (data_size >= 1 && data[0] == 0x04) return "Vacation";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 269: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 270: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 271: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 272: {
        if (data_size >= 1 && data[0] == 0x00) return "Normal";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "X-High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 273: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 274: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 275: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 276: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 277: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 278: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 279: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 280: {
        if (data_size >= 1 && data[0] == 0x00) return "Beep";
        if (data_size >= 1 && data[0] == 0x01) return "Continuous Tone";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 281: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 282: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 283: {
        if (data_size >= 1 && data[0] == 0x00) return "n/a";
        if (data_size >= 1 && data[0] == 0x01) return "Bake";
        if (data_size >= 1 && data[0] == 0x02) return "Broil";
        if (data_size >= 1 && data[0] == 0x03) return "Self Clean";
        if (data_size >= 1 && data[0] == 0x04) return "Steam Clean";
        if (data_size >= 1 && data[0] == 0x05) return "Convection Bake";
        if (data_size >= 1 && data[0] == 0x06) return "Convection Bake Multi";
        if (data_size >= 1 && data[0] == 0x07) return "Convection Roast";
        if (data_size >= 1 && data[0] == 0x08) return "Convection Broil";
        if (data_size >= 1 && data[0] == 0x09) return "Warm";
        if (data_size >= 1 && data[0] == 0x0a) return "Proof";
        if (data_size >= 1 && data[0] == 0x0b) return "Sabbath Bake";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 284: {
        if (data_size >= 1 && data[0] == 0x00) return "Manual";
        if (data_size >= 1 && data[0] == 0x01) return "Haier Knob Auto On";
        if (data_size >= 1 && data[0] == 0x02) return "Auto Off";
        if (data_size >= 1 && data[0] == 0x03) return "Auto On";
        if (data_size >= 1 && data[0] == 0xff) return "Unset";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 285: {
        if (data_size >= 1 && data[0] == 0x00) return "Low";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "Convection Low";
        if (data_size >= 1 && data[0] == 0x03) return "Convection High";
        if (data_size >= 1 && data[0] == 0x04) return "Convection Crisp";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 286: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 287: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 288: {
        if (data_size >= 1 && data[0] == 0x00) return "Auto";
        if (data_size >= 1 && data[0] == 0x01) return "Manual";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 289: {
        if (data_size >= 1 && data[0] == 0x00) return "Unknown";
        if (data_size >= 1 && data[0] == 0x01) return "Extended";
        if (data_size >= 1 && data[0] == 0x02) return "Retracted";
        if (data_size >= 1 && data[0] == 0x03) return "Extending";
        if (data_size >= 1 && data[0] == 0x04) return "Retracting";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 290: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 291: {
        if (data_size >= 1 && data[0] == 0x00) return "Manual";
        if (data_size >= 1 && data[0] == 0x01) return "Haier Knob Auto On";
        if (data_size >= 1 && data[0] == 0x02) return "Auto Off";
        if (data_size >= 1 && data[0] == 0x03) return "Auto On";
        if (data_size >= 1 && data[0] == 0xff) return "Unset";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 292: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 293: {
        if (data_size >= 1 && data[0] == 0x00) return "Standard";
        if (data_size >= 1 && data[0] == 0x01) return "Warmer";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 294: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 295: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 296: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 297: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 298: {
        if (data_size >= 1 && data[0] == 0x00) return "No Mode";
        if (data_size >= 1 && data[0] == 0x01) return "Bake No Option";
        if (data_size >= 1 && data[0] == 0x02) return "Bake Probe";
        if (data_size >= 1 && data[0] == 0x03) return "Bake Delay Start";
        if (data_size >= 1 && data[0] == 0x04) return "Bake Timed Warm";
        if (data_size >= 1 && data[0] == 0x05) return "Bake Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x06) return "Bake Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x07) return "Bake Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x08) return "Bake Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x09) return "Bake Timed TwoTemp Delay Start";
        if (data_size >= 1 && data[0] == 0x0a) return "Bake Sabbath";
        if (data_size >= 1 && data[0] == 0x0b) return "Broil Low";
        if (data_size >= 1 && data[0] == 0x0c) return "Broil High";
        if (data_size >= 1 && data[0] == 0x0d) return "Proof No Option";
        if (data_size >= 1 && data[0] == 0x0e) return "Proof Delay Start";
        if (data_size >= 1 && data[0] == 0x0f) return "Warm No Option";
        if (data_size >= 1 && data[0] == 0x10) return "Warm Probe";
        if (data_size >= 1 && data[0] == 0x11) return "Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x12) return "Convection Bake No Option";
        if (data_size >= 1 && data[0] == 0x13) return "Convection Bake Probe";
        if (data_size >= 1 && data[0] == 0x14) return "Convection Bake Delay Start";
        if (data_size >= 1 && data[0] == 0x15) return "Convection Bake Timed Warm";
        if (data_size >= 1 && data[0] == 0x16) return "Convection Bake Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x17) return "Convection Bake Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x18) return "Convection Bake Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x19) return "Convection Bake Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x1a) return "Convection Bake Timed TwoTemp Delay Start";
        if (data_size >= 1 && data[0] == 0x1b) return "Convection Multi-Bake No Option";
        if (data_size >= 1 && data[0] == 0x1c) return "Convection Multi-Bake Probe";
        if (data_size >= 1 && data[0] == 0x1d) return "Convection Multi-Bake Delay Start";
        if (data_size >= 1 && data[0] == 0x1e) return "Convection Multi-Bake Timed Warm";
        if (data_size >= 1 && data[0] == 0x1f) return "Convection Multi-Bake Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x20) return "Convection Multi-Bake Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x21) return "Convection Multi-Bake Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x22) return "Convection Multi-Bake Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x23) return "Convection Multi-Bake Timed Two-Temp Delay Start";
        if (data_size >= 1 && data[0] == 0x24) return "Convection Roast No Option";
        if (data_size >= 1 && data[0] == 0x25) return "Convection Roast Probe";
        if (data_size >= 1 && data[0] == 0x26) return "Convection Roast Delay Start";
        if (data_size >= 1 && data[0] == 0x27) return "Convection Roast Timed Warm";
        if (data_size >= 1 && data[0] == 0x28) return "Convection Roast Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x29) return "Convection Roast Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x2a) return "Convection Roast Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x2b) return "Convection Roast Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x2c) return "Convection Roast Timed Two-Temp Delay Start";
        if (data_size >= 1 && data[0] == 0x2d) return "Convection Broil Low No Option";
        if (data_size >= 1 && data[0] == 0x2e) return "Convection Broil High No Option";
        if (data_size >= 1 && data[0] == 0x2f) return "Convection Broil Crisp No Option";
        if (data_size >= 1 && data[0] == 0x30) return "Convection Broil Crisp Probe";
        if (data_size >= 1 && data[0] == 0x31) return "Custom Self Clean";
        if (data_size >= 1 && data[0] == 0x32) return "Custom Self Clean Delay Start";
        if (data_size >= 1 && data[0] == 0x33) return "Steam Clean";
        if (data_size >= 1 && data[0] == 0x34) return "Steam Clean Delay Start";
        if (data_size >= 1 && data[0] == 0x35) return "Dual Broil Low No Option";
        if (data_size >= 1 && data[0] == 0x36) return "Dual Broil High No Option";
        if (data_size >= 1 && data[0] == 0x37) return "FCT_NoOption";
        if (data_size >= 1 && data[0] == 0x38) return "Frozen Snacks No Option";
        if (data_size >= 1 && data[0] == 0x39) return "Frozen Snacks Multi NoOption";
        if (data_size >= 1 && data[0] == 0x3a) return "Frozen Pizza No Option";
        if (data_size >= 1 && data[0] == 0x3b) return "Frozen PizzaMulti No Option";
        if (data_size >= 1 && data[0] == 0x3c) return "Baked Goods No Option";
        if (data_size >= 1 && data[0] == 0x3d) return "Frozen Snacks Delay Start";
        if (data_size >= 1 && data[0] == 0x3e) return "Frozen Snacks Multi Delay Start";
        if (data_size >= 1 && data[0] == 0x3f) return "Frozen Pizza Delay Start";
        if (data_size >= 1 && data[0] == 0x40) return "Frozen Pizza Multi Delay Start";
        if (data_size >= 1 && data[0] == 0x41) return "Baked Goods Delay Start";
        if (data_size >= 1 && data[0] == 0x42) return "Special 6 No Option";
        if (data_size >= 1 && data[0] == 0x43) return "Special 6 Probe";
        if (data_size >= 1 && data[0] == 0x44) return "Special 6 Delay Start";
        if (data_size >= 1 && data[0] == 0x45) return "Special 6 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x46) return "Special 7 No Option";
        if (data_size >= 1 && data[0] == 0x47) return "Special 7 Probe";
        if (data_size >= 1 && data[0] == 0x48) return "Special 7 Delay Start";
        if (data_size >= 1 && data[0] == 0x49) return "Special 7 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x4a) return "Special 8 No Option";
        if (data_size >= 1 && data[0] == 0x4b) return "Special 8 Probe";
        if (data_size >= 1 && data[0] == 0x4c) return "Special 8 Delay Start";
        if (data_size >= 1 && data[0] == 0x4d) return "Special 8 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x4e) return "Special 9 No Option";
        if (data_size >= 1 && data[0] == 0x4f) return "Special 9 Probe";
        if (data_size >= 1 && data[0] == 0x50) return "Special 9 Delay Start";
        if (data_size >= 1 && data[0] == 0x51) return "Special 9 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x52) return "Special 10 No Option";
        if (data_size >= 1 && data[0] == 0x53) return "Special 10 Probe";
        if (data_size >= 1 && data[0] == 0x54) return "Special 10 Delay Start";
        if (data_size >= 1 && data[0] == 0x55) return "Special 10 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x56) return "Special 11 No Option";
        if (data_size >= 1 && data[0] == 0x57) return "Special 11 Probe";
        if (data_size >= 1 && data[0] == 0x58) return "Special 11 Delay Start";
        if (data_size >= 1 && data[0] == 0x59) return "Special 11 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x5a) return "Special 12 No Option";
        if (data_size >= 1 && data[0] == 0x5b) return "Special 12 Probe";
        if (data_size >= 1 && data[0] == 0x5c) return "Special 12 Delay Start";
        if (data_size >= 1 && data[0] == 0x5d) return "Special 12 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x5e) return "Special 13 No Option";
        if (data_size >= 1 && data[0] == 0x5f) return "Special 13 Probe";
        if (data_size >= 1 && data[0] == 0x60) return "Special 13 Delay Start";
        if (data_size >= 1 && data[0] == 0x61) return "Special 13 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x62) return "Special 14 No Option";
        if (data_size >= 1 && data[0] == 0x63) return "Special 14 Probe";
        if (data_size >= 1 && data[0] == 0x64) return "Special 14 Delay Start";
        if (data_size >= 1 && data[0] == 0x65) return "Special 14 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x66) return "Special 15 No Option";
        if (data_size >= 1 && data[0] == 0x67) return "Special 15 Probe";
        if (data_size >= 1 && data[0] == 0x68) return "Special 15 Delay Start";
        if (data_size >= 1 && data[0] == 0x69) return "Special 15 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x6a) return "Special 16 No Option";
        if (data_size >= 1 && data[0] == 0x6b) return "Special 16 Probe";
        if (data_size >= 1 && data[0] == 0x6c) return "Special 16 Delay Start";
        if (data_size >= 1 && data[0] == 0x6d) return "Special 16 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x6e) return "Special 17 No Option";
        if (data_size >= 1 && data[0] == 0x6f) return "Special 17 Probe";
        if (data_size >= 1 && data[0] == 0x70) return "Special 17 Delay Start";
        if (data_size >= 1 && data[0] == 0x71) return "Special 17 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x72) return "Special 18 No Option";
        if (data_size >= 1 && data[0] == 0x73) return "Special 18 Probe";
        if (data_size >= 1 && data[0] == 0x74) return "Special 18 Delay Start";
        if (data_size >= 1 && data[0] == 0x75) return "Special 18 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x76) return "Special 19 No Option";
        if (data_size >= 1 && data[0] == 0x77) return "Special 19 Probe";
        if (data_size >= 1 && data[0] == 0x78) return "Special 19 Delay Start";
        if (data_size >= 1 && data[0] == 0x79) return "Special 19 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x7a) return "Special 20 No Option";
        if (data_size >= 1 && data[0] == 0x7b) return "Special 20 Probe";
        if (data_size >= 1 && data[0] == 0x7c) return "Special 20 Delay Start";
        if (data_size >= 1 && data[0] == 0x7d) return "Special 20 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x7e) return "Special 21 No Option";
        if (data_size >= 1 && data[0] == 0x7f) return "Special 21 Probe";
        if (data_size >= 1 && data[0] == 0x80) return "Special 21 Delay Start";
        if (data_size >= 1 && data[0] == 0x81) return "Special 21 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x82) return "Special 22 No Option";
        if (data_size >= 1 && data[0] == 0x83) return "Special 22 Probe";
        if (data_size >= 1 && data[0] == 0x84) return "Special 22 Delay Start";
        if (data_size >= 1 && data[0] == 0x85) return "Special 22 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x86) return "Special 23 No Option";
        if (data_size >= 1 && data[0] == 0x87) return "Special 23 Probe";
        if (data_size >= 1 && data[0] == 0x88) return "Special 23 Delay Start";
        if (data_size >= 1 && data[0] == 0x89) return "Special 23 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x8a) return "Special 24 No Option";
        if (data_size >= 1 && data[0] == 0x8b) return "Special 24 Probe";
        if (data_size >= 1 && data[0] == 0x8c) return "Special 24 Delay Start";
        if (data_size >= 1 && data[0] == 0x8d) return "Special 24 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x8e) return "Special 25 No Option";
        if (data_size >= 1 && data[0] == 0x8f) return "Special 25 Probe";
        if (data_size >= 1 && data[0] == 0x90) return "Special 25 Delay Start";
        if (data_size >= 1 && data[0] == 0x91) return "Special 25 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x92) return "Special 26 No Option";
        if (data_size >= 1 && data[0] == 0x93) return "Special 26 Probe";
        if (data_size >= 1 && data[0] == 0x94) return "Special 26 Delay Start";
        if (data_size >= 1 && data[0] == 0x95) return "Special 26 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x96) return "Special 27 No Option";
        if (data_size >= 1 && data[0] == 0x97) return "Special 27 Probe";
        if (data_size >= 1 && data[0] == 0x98) return "Special 27 Delay Start";
        if (data_size >= 1 && data[0] == 0x99) return "Special 27 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x9a) return "Special 28 No Option";
        if (data_size >= 1 && data[0] == 0x9b) return "Special 28 Probe";
        if (data_size >= 1 && data[0] == 0x9c) return "Special 28 Delay Start";
        if (data_size >= 1 && data[0] == 0x9d) return "Special 28 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x9e) return "Air Fry No Option";
        if (data_size >= 1 && data[0] == 0x9f) return "Air Fry Probe";
        if (data_size >= 1 && data[0] == 0xa0) return "Air Fry Delay Start";
        if (data_size >= 1 && data[0] == 0xa1) return "Air Fry Probe Delay Start";
        if (data_size >= 1 && data[0] == 0xa2) return "Special 30 No Option";
        if (data_size >= 1 && data[0] == 0xa3) return "Special 30 Probe";
        if (data_size >= 1 && data[0] == 0xa4) return "Special 30 Delay Start";
        if (data_size >= 1 && data[0] == 0xa5) return "Special 30 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0xa6) return "Crisp No Option";
        if (data_size >= 1 && data[0] == 0xa7) return "Crisp Probe";
        if (data_size >= 1 && data[0] == 0xa8) return "Crisp Delay Start";
        if (data_size >= 1 && data[0] == 0xa9) return "Crisp Probe Delay Start";
        if (data_size >= 1 && data[0] == 0xaa) return "Reheat High No Option";
        if (data_size >= 1 && data[0] == 0xab) return "Reheat High Delay Start";
        if (data_size >= 1 && data[0] == 0xac) return "Reheat Low No Option";
        if (data_size >= 1 && data[0] == 0xad) return "Reheat Low Delay Start";
        if (data_size >= 1 && data[0] == 0xae) return "Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xaf) return "Convection Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb0) return "Convection Multi-Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb1) return "Convection Roast Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb2) return "Convection Broil High Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb3) return "Traditional Bake";
        if (data_size >= 1 && data[0] == 0xb4) return "Traditional Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb5) return "Pastry Plus";
        if (data_size >= 1 && data[0] == 0xb6) return "Pastry Plus Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb7) return "Pizza";
        if (data_size >= 1 && data[0] == 0xb8) return "Defrost";
        if (data_size >= 1 && data[0] == 0xb9) return "Dynamic Broil";
        if (data_size >= 1 && data[0] == 0xba) return "Toast";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 299: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 300: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 301: {
        if (data_size >= 1 && data[0] == 0x00) return "Default";
        if (data_size >= 1 && data[0] == 0x01) return "Open Door Request";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 302: {
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "Dim";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 303: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 304: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 305: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop";
        if (data_size >= 1 && data[0] == 0x01) return "Start";
        if (data_size >= 1 && data[0] == 0x02) return "Update";
        if (data_size >= 1 && data[0] == 0x03) return "Pause";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 306: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 307: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 308: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 309: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 310: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 311: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 312: {
        if (data_size >= 1 && data[0] == 0x00) return "No Mode";
        if (data_size >= 1 && data[0] == 0x01) return "Bake No Option";
        if (data_size >= 1 && data[0] == 0x02) return "Bake Probe";
        if (data_size >= 1 && data[0] == 0x03) return "Bake Delay Start";
        if (data_size >= 1 && data[0] == 0x04) return "Bake Timed Warm";
        if (data_size >= 1 && data[0] == 0x05) return "Bake Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x06) return "Bake Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x07) return "Bake Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x08) return "Bake Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x09) return "Bake Timed TwoTemp Delay Start";
        if (data_size >= 1 && data[0] == 0x0a) return "Bake Sabbath";
        if (data_size >= 1 && data[0] == 0x0b) return "Broil Low";
        if (data_size >= 1 && data[0] == 0x0c) return "Broil High";
        if (data_size >= 1 && data[0] == 0x0d) return "Proof No Option";
        if (data_size >= 1 && data[0] == 0x0e) return "Proof Delay Start";
        if (data_size >= 1 && data[0] == 0x0f) return "Warm No Option";
        if (data_size >= 1 && data[0] == 0x10) return "Warm Probe";
        if (data_size >= 1 && data[0] == 0x11) return "Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x12) return "Convection Bake No Option";
        if (data_size >= 1 && data[0] == 0x13) return "Convection Bake Probe";
        if (data_size >= 1 && data[0] == 0x14) return "Convection Bake Delay Start";
        if (data_size >= 1 && data[0] == 0x15) return "Convection Bake Timed Warm";
        if (data_size >= 1 && data[0] == 0x16) return "Convection Bake Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x17) return "Convection Bake Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x18) return "Convection Bake Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x19) return "Convection Bake Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x1a) return "Convection Bake Timed TwoTemp Delay Start";
        if (data_size >= 1 && data[0] == 0x1b) return "Convection Multi-Bake No Option";
        if (data_size >= 1 && data[0] == 0x1c) return "Convection Multi-Bake Probe";
        if (data_size >= 1 && data[0] == 0x1d) return "Convection Multi-Bake Delay Start";
        if (data_size >= 1 && data[0] == 0x1e) return "Convection Multi-Bake Timed Warm";
        if (data_size >= 1 && data[0] == 0x1f) return "Convection Multi-Bake Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x20) return "Convection Multi-Bake Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x21) return "Convection Multi-Bake Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x22) return "Convection Multi-Bake Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x23) return "Convection Multi-Bake Timed Two-Temp Delay Start";
        if (data_size >= 1 && data[0] == 0x24) return "Convection Roast No Option";
        if (data_size >= 1 && data[0] == 0x25) return "Convection Roast Probe";
        if (data_size >= 1 && data[0] == 0x26) return "Convection Roast Delay Start";
        if (data_size >= 1 && data[0] == 0x27) return "Convection Roast Timed Warm";
        if (data_size >= 1 && data[0] == 0x28) return "Convection Roast Timed Two-Temp";
        if (data_size >= 1 && data[0] == 0x29) return "Convection Roast Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x2a) return "Convection Roast Timed Shutoff Delay Start";
        if (data_size >= 1 && data[0] == 0x2b) return "Convection Roast Timed Warm Delay Start";
        if (data_size >= 1 && data[0] == 0x2c) return "Convection Roast Timed Two-Temp Delay Start";
        if (data_size >= 1 && data[0] == 0x2d) return "Convection Broil Low No Option";
        if (data_size >= 1 && data[0] == 0x2e) return "Convection Broil High No Option";
        if (data_size >= 1 && data[0] == 0x2f) return "Convection Broil Crisp No Option";
        if (data_size >= 1 && data[0] == 0x30) return "Convection Broil Crisp Probe";
        if (data_size >= 1 && data[0] == 0x31) return "Custom Self Clean";
        if (data_size >= 1 && data[0] == 0x32) return "Custom Self Clean Delay Start";
        if (data_size >= 1 && data[0] == 0x33) return "Steam Clean";
        if (data_size >= 1 && data[0] == 0x34) return "Steam Clean Delay Start";
        if (data_size >= 1 && data[0] == 0x35) return "Dual Broil Low No Option";
        if (data_size >= 1 && data[0] == 0x36) return "Dual Broil High No Option";
        if (data_size >= 1 && data[0] == 0x37) return "FCT_NoOption";
        if (data_size >= 1 && data[0] == 0x38) return "Frozen Snacks No Option";
        if (data_size >= 1 && data[0] == 0x39) return "Frozen Snacks Multi NoOption";
        if (data_size >= 1 && data[0] == 0x3a) return "Frozen Pizza No Option";
        if (data_size >= 1 && data[0] == 0x3b) return "Frozen PizzaMulti No Option";
        if (data_size >= 1 && data[0] == 0x3c) return "Baked Goods No Option";
        if (data_size >= 1 && data[0] == 0x3d) return "Frozen Snacks Delay Start";
        if (data_size >= 1 && data[0] == 0x3e) return "Frozen Snacks Multi Delay Start";
        if (data_size >= 1 && data[0] == 0x3f) return "Frozen Pizza Delay Start";
        if (data_size >= 1 && data[0] == 0x40) return "Frozen Pizza Multi Delay Start";
        if (data_size >= 1 && data[0] == 0x41) return "Baked Goods Delay Start";
        if (data_size >= 1 && data[0] == 0x42) return "Special 6 No Option";
        if (data_size >= 1 && data[0] == 0x43) return "Special 6 Probe";
        if (data_size >= 1 && data[0] == 0x44) return "Special 6 Delay Start";
        if (data_size >= 1 && data[0] == 0x45) return "Special 6 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x46) return "Special 7 No Option";
        if (data_size >= 1 && data[0] == 0x47) return "Special 7 Probe";
        if (data_size >= 1 && data[0] == 0x48) return "Special 7 Delay Start";
        if (data_size >= 1 && data[0] == 0x49) return "Special 7 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x4a) return "Special 8 No Option";
        if (data_size >= 1 && data[0] == 0x4b) return "Special 8 Probe";
        if (data_size >= 1 && data[0] == 0x4c) return "Special 8 Delay Start";
        if (data_size >= 1 && data[0] == 0x4d) return "Special 8 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x4e) return "Special 9 No Option";
        if (data_size >= 1 && data[0] == 0x4f) return "Special 9 Probe";
        if (data_size >= 1 && data[0] == 0x50) return "Special 9 Delay Start";
        if (data_size >= 1 && data[0] == 0x51) return "Special 9 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x52) return "Special 10 No Option";
        if (data_size >= 1 && data[0] == 0x53) return "Special 10 Probe";
        if (data_size >= 1 && data[0] == 0x54) return "Special 10 Delay Start";
        if (data_size >= 1 && data[0] == 0x55) return "Special 10 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x56) return "Special 11 No Option";
        if (data_size >= 1 && data[0] == 0x57) return "Special 11 Probe";
        if (data_size >= 1 && data[0] == 0x58) return "Special 11 Delay Start";
        if (data_size >= 1 && data[0] == 0x59) return "Special 11 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x5a) return "Special 12 No Option";
        if (data_size >= 1 && data[0] == 0x5b) return "Special 12 Probe";
        if (data_size >= 1 && data[0] == 0x5c) return "Special 12 Delay Start";
        if (data_size >= 1 && data[0] == 0x5d) return "Special 12 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x5e) return "Special 13 No Option";
        if (data_size >= 1 && data[0] == 0x5f) return "Special 13 Probe";
        if (data_size >= 1 && data[0] == 0x60) return "Special 13 Delay Start";
        if (data_size >= 1 && data[0] == 0x61) return "Special 13 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x62) return "Special 14 No Option";
        if (data_size >= 1 && data[0] == 0x63) return "Special 14 Probe";
        if (data_size >= 1 && data[0] == 0x64) return "Special 14 Delay Start";
        if (data_size >= 1 && data[0] == 0x65) return "Special 14 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x66) return "Special 15 No Option";
        if (data_size >= 1 && data[0] == 0x67) return "Special 15 Probe";
        if (data_size >= 1 && data[0] == 0x68) return "Special 15 Delay Start";
        if (data_size >= 1 && data[0] == 0x69) return "Special 15 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x6a) return "Special 16 No Option";
        if (data_size >= 1 && data[0] == 0x6b) return "Special 16 Probe";
        if (data_size >= 1 && data[0] == 0x6c) return "Special 16 Delay Start";
        if (data_size >= 1 && data[0] == 0x6d) return "Special 16 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x6e) return "Special 17 No Option";
        if (data_size >= 1 && data[0] == 0x6f) return "Special 17 Probe";
        if (data_size >= 1 && data[0] == 0x70) return "Special 17 Delay Start";
        if (data_size >= 1 && data[0] == 0x71) return "Special 17 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x72) return "Special 18 No Option";
        if (data_size >= 1 && data[0] == 0x73) return "Special 18 Probe";
        if (data_size >= 1 && data[0] == 0x74) return "Special 18 Delay Start";
        if (data_size >= 1 && data[0] == 0x75) return "Special 18 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x76) return "Special 19 No Option";
        if (data_size >= 1 && data[0] == 0x77) return "Special 19 Probe";
        if (data_size >= 1 && data[0] == 0x78) return "Special 19 Delay Start";
        if (data_size >= 1 && data[0] == 0x79) return "Special 19 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x7a) return "Special 20 No Option";
        if (data_size >= 1 && data[0] == 0x7b) return "Special 20 Probe";
        if (data_size >= 1 && data[0] == 0x7c) return "Special 20 Delay Start";
        if (data_size >= 1 && data[0] == 0x7d) return "Special 20 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x7e) return "Special 21 No Option";
        if (data_size >= 1 && data[0] == 0x7f) return "Special 21 Probe";
        if (data_size >= 1 && data[0] == 0x80) return "Special 21 Delay Start";
        if (data_size >= 1 && data[0] == 0x81) return "Special 21 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x82) return "Special 22 No Option";
        if (data_size >= 1 && data[0] == 0x83) return "Special 22 Probe";
        if (data_size >= 1 && data[0] == 0x84) return "Special 22 Delay Start";
        if (data_size >= 1 && data[0] == 0x85) return "Special 22 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x86) return "Special 23 No Option";
        if (data_size >= 1 && data[0] == 0x87) return "Special 23 Probe";
        if (data_size >= 1 && data[0] == 0x88) return "Special 23 Delay Start";
        if (data_size >= 1 && data[0] == 0x89) return "Special 23 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x8a) return "Special 24 No Option";
        if (data_size >= 1 && data[0] == 0x8b) return "Special 24 Probe";
        if (data_size >= 1 && data[0] == 0x8c) return "Special 24 Delay Start";
        if (data_size >= 1 && data[0] == 0x8d) return "Special 24 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x8e) return "Special 25 No Option";
        if (data_size >= 1 && data[0] == 0x8f) return "Special 25 Probe";
        if (data_size >= 1 && data[0] == 0x90) return "Special 25 Delay Start";
        if (data_size >= 1 && data[0] == 0x91) return "Special 25 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x92) return "Special 26 No Option";
        if (data_size >= 1 && data[0] == 0x93) return "Special 26 Probe";
        if (data_size >= 1 && data[0] == 0x94) return "Special 26 Delay Start";
        if (data_size >= 1 && data[0] == 0x95) return "Special 26 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x96) return "Special 27 No Option";
        if (data_size >= 1 && data[0] == 0x97) return "Special 27 Probe";
        if (data_size >= 1 && data[0] == 0x98) return "Special 27 Delay Start";
        if (data_size >= 1 && data[0] == 0x99) return "Special 27 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x9a) return "Special 28 No Option";
        if (data_size >= 1 && data[0] == 0x9b) return "Special 28 Probe";
        if (data_size >= 1 && data[0] == 0x9c) return "Special 28 Delay Start";
        if (data_size >= 1 && data[0] == 0x9d) return "Special 28 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0x9e) return "Air Fry No Option";
        if (data_size >= 1 && data[0] == 0x9f) return "Air Fry Probe";
        if (data_size >= 1 && data[0] == 0xa0) return "Air Fry Delay Start";
        if (data_size >= 1 && data[0] == 0xa1) return "Air Fry Probe Delay Start";
        if (data_size >= 1 && data[0] == 0xa2) return "Special 30 No Option";
        if (data_size >= 1 && data[0] == 0xa3) return "Special 30 Probe";
        if (data_size >= 1 && data[0] == 0xa4) return "Special 30 Delay Start";
        if (data_size >= 1 && data[0] == 0xa5) return "Special 30 Probe Delay Start";
        if (data_size >= 1 && data[0] == 0xa6) return "Crisp No Option";
        if (data_size >= 1 && data[0] == 0xa7) return "Crisp Probe";
        if (data_size >= 1 && data[0] == 0xa8) return "Crisp Delay Start";
        if (data_size >= 1 && data[0] == 0xa9) return "Crisp Probe Delay Start";
        if (data_size >= 1 && data[0] == 0xaa) return "Reheat High No Option";
        if (data_size >= 1 && data[0] == 0xab) return "Reheat High Delay Start";
        if (data_size >= 1 && data[0] == 0xac) return "Reheat Low No Option";
        if (data_size >= 1 && data[0] == 0xad) return "Reheat Low Delay Start";
        if (data_size >= 1 && data[0] == 0xae) return "Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xaf) return "Convection Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb0) return "Convection Multi-Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb1) return "Convection Roast Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb2) return "Convection Broil High Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb3) return "Traditional Bake";
        if (data_size >= 1 && data[0] == 0xb4) return "Traditional Bake Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb5) return "Pastry Plus";
        if (data_size >= 1 && data[0] == 0xb6) return "Pastry Plus Rapid Preheat";
        if (data_size >= 1 && data[0] == 0xb7) return "Pizza";
        if (data_size >= 1 && data[0] == 0xb8) return "Defrost";
        if (data_size >= 1 && data[0] == 0xb9) return "Dynamic Broil";
        if (data_size >= 1 && data[0] == 0xba) return "Toast";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 313: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 314: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 315: {
        if (data_size >= 1 && data[0] == 0x00) return "Default";
        if (data_size >= 1 && data[0] == 0x01) return "Open Door Request";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 316: {
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "Dim";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 317: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 318: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 319: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop";
        if (data_size >= 1 && data[0] == 0x01) return "Start";
        if (data_size >= 1 && data[0] == 0x02) return "Update";
        if (data_size >= 1 && data[0] == 0x03) return "Pause";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 320: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 321: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 322: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 323: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 324: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 325: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 326: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 327: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 328: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 329: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 330: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 331: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Required";
        if (data_size >= 1 && data[0] == 0x01) return "Required";
        if (data_size >= 1 && data[0] == 0xff) return "Dont Care";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 332: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop";
        if (data_size >= 1 && data[0] == 0x01) return "Start";
        if (data_size >= 1 && data[0] == 0x02) return "Updated";
        if (data_size >= 1 && data[0] == 0x03) return "Pause";
        if (data_size >= 1 && data[0] == 0x04) return "Resume";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 333: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Required";
        if (data_size >= 1 && data[0] == 0x01) return "Required";
        if (data_size >= 1 && data[0] == 0xff) return "Dont Care";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 334: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 335: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 336: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 337: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 338: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 339: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 340: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 341: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 342: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 343: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 344: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 345: {
        if (data_size >= 1 && data[0] == 0x00) return "Generic";
        if (data_size >= 1 && data[0] == 0x01) return "Stainless";
        if (data_size >= 1 && data[0] == 0x02) return "Cast Iron";
        if (data_size >= 1 && data[0] == 0xff) return "Not Set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 346: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 347: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 348: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 349: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 350: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Med";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "Boost";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 351: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Dim";
        if (data_size >= 1 && data[0] == 0x02) return "High";
        if (data_size >= 1 && data[0] == 0x03) return "Med";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 352: {
        if (data_size >= 1 && data[0] == 0x01) return "Dim";
        if (data_size >= 1 && data[0] == 0x02) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 353: {
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Med";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "Boost";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 354: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 355: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 356: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 357: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 358: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 359: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 360: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 361: {
        if (data_size >= 1 && data[0] == 0x00) return "Invalid";
        if (data_size >= 1 && data[0] == 0x01) return "3000K";
        if (data_size >= 1 && data[0] == 0x02) return "4000K";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 362: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Auto";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 363: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 364: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 365: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 366: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 367: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 368: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 369: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 370: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 371: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 372: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 373: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 374: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 375: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 376: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 377: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 378: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 379: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 380: {
        if (data_size >= 1 && data[0] == 0x00) return "Traffic Light";
        if (data_size >= 1 && data[0] == 0x01) return "Profile";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 381: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 382: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 383: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 384: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 385: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 386: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 387: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 388: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 389: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 390: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "1.75 oz";
        if (data_size >= 1 && data[0] == 0x02) return "2 oz";
        if (data_size >= 1 && data[0] == 0x03) return "2.25 oz";
        if (data_size >= 1 && data[0] == 0x04) return "3 oz";
        if (data_size >= 1 && data[0] == 0x05) return "3.5 oz";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 391: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "1 Potato";
        if (data_size >= 1 && data[0] == 0x02) return "2 Potato";
        if (data_size >= 1 && data[0] == 0x03) return "3 Potato";
        if (data_size >= 1 && data[0] == 0x04) return "4 Potato";
        if (data_size >= 1 && data[0] == 0x05) return "5 Potato";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 392: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "1 Cup";
        if (data_size >= 1 && data[0] == 0x02) return "2 Cup";
        if (data_size >= 1 && data[0] == 0x03) return "3 Cup";
        if (data_size >= 1 && data[0] == 0x04) return "4 Cup";
        if (data_size >= 1 && data[0] == 0x05) return "5 Cup";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 393: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "1 Slice";
        if (data_size >= 1 && data[0] == 0x02) return "2 Slice";
        if (data_size >= 1 && data[0] == 0x03) return "3 Slice";
        if (data_size >= 1 && data[0] == 0x04) return "4 Slice";
        if (data_size >= 1 && data[0] == 0x05) return "5 Slice";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 394: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "0.5 cups";
        if (data_size >= 1 && data[0] == 0x02) return "1 cup";
        if (data_size >= 1 && data[0] == 0x03) return "1.5 cups";
        if (data_size >= 1 && data[0] == 0x04) return "2 cups";
        if (data_size >= 1 && data[0] == 0x05) return "2.5 cups";
        if (data_size >= 1 && data[0] == 0x06) return "3 cups";
        if (data_size >= 1 && data[0] == 0x07) return "4 cups";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 395: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "0.5 cups";
        if (data_size >= 1 && data[0] == 0x02) return "1 cup";
        if (data_size >= 1 && data[0] == 0x03) return "1.5 cups";
        if (data_size >= 1 && data[0] == 0x04) return "2 cups";
        if (data_size >= 1 && data[0] == 0x05) return "2.5 cups";
        if (data_size >= 1 && data[0] == 0x06) return "3 cups";
        if (data_size >= 1 && data[0] == 0x07) return "4 cups";
        if (data_size >= 1 && data[0] == 0x08) return "Sensed Canned";
        if (data_size >= 1 && data[0] == 0x09) return "1 8oz Can";
        if (data_size >= 1 && data[0] == 0x0a) return "1.5 8oz Can";
        if (data_size >= 1 && data[0] == 0x0b) return "2 8oz Can";
        if (data_size >= 1 && data[0] == 0x0c) return "2.5 8oz Can";
        if (data_size >= 1 && data[0] == 0x0d) return "3 8oz Can";
        if (data_size >= 1 && data[0] == 0x0e) return "3.5 8oz Can";
        if (data_size >= 1 && data[0] == 0x0f) return "4 8oz Can";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 396: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Plate";
        if (data_size >= 1 && data[0] == 0x02) return "Pasta";
        if (data_size >= 1 && data[0] == 0x03) return "Soup";
        if (data_size >= 1 && data[0] == 0x04) return "Vegetables";
        if (data_size >= 1 && data[0] == 0x05) return "Pizza";
        if (data_size >= 1 && data[0] == 0x06) return "Beef";
        if (data_size >= 1 && data[0] == 0x07) return "Poultry";
        if (data_size >= 1 && data[0] == 0x08) return "Pork";
        if (data_size >= 1 && data[0] == 0x09) return "Fish";
        if (data_size >= 1 && data[0] == 0x0a) return "Auto";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 397: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Set";
        if (data_size >= 1 && data[0] == 0x01) return "Set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 398: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x02) return "Paused";
        if (data_size >= 1 && data[0] == 0x03) return "End of Cycle";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 399: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 400: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 401: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 402: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 403: {
        if (data_size >= 1 && data[0] == 0x00) return "Subtract Cook Time";
        if (data_size >= 1 && data[0] == 0x01) return "Add Cook Time";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 404: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "1 cup";
        if (data_size >= 1 && data[0] == 0x02) return "2 cups";
        if (data_size >= 1 && data[0] == 0x03) return "3 cups";
        if (data_size >= 1 && data[0] == 0x04) return "4 cups";
        if (data_size >= 1 && data[0] == 0x05) return "5 cups";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 405: {
        if (data_size >= 1 && data[0] == 0x00) return "Subtract Time";
        if (data_size >= 1 && data[0] == 0x01) return "Add Time";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 406: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 407: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Meat";
        if (data_size >= 1 && data[0] == 0x02) return "Poultry";
        if (data_size >= 1 && data[0] == 0x03) return "Fish";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 408: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 409: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Pizza";
        if (data_size >= 1 && data[0] == 0x02) return "Muffins";
        if (data_size >= 1 && data[0] == 0x03) return "Biscuit/Dinner Roll";
        if (data_size >= 1 && data[0] == 0x04) return "French Fries";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 410: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Beef";
        if (data_size >= 1 && data[0] == 0x02) return "Whole Chicken";
        if (data_size >= 1 && data[0] == 0x03) return "Turkey Breast";
        if (data_size >= 1 && data[0] == 0x04) return "Pork";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 411: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 412: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 413: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Ground Beef";
        if (data_size >= 1 && data[0] == 0x02) return "Fish";
        if (data_size >= 1 && data[0] == 0x03) return "Chicken";
        if (data_size >= 1 && data[0] == 0x04) return "Pork";
        if (data_size >= 1 && data[0] == 0x05) return "Turkey";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 414: {
        if (data_size >= 1 && data[0] == 0x00) return "Sensed";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 415: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 416: {
        if (data_size >= 1 && data[0] == 0x00) return "Butter";
        if (data_size >= 1 && data[0] == 0x01) return "Caramel";
        if (data_size >= 1 && data[0] == 0x02) return "Chocolate Chip";
        if (data_size >= 1 && data[0] == 0x03) return "Marshmallow";
        if (data_size >= 1 && data[0] == 0x04) return "Cheese";
        if (data_size >= 1 && data[0] == 0x05) return "Soften Butter";
        if (data_size >= 1 && data[0] == 0x06) return "Soften Ice Cream";
        if (data_size >= 1 && data[0] == 0x07) return "Soften Cream Cheese";
        if (data_size >= 1 && data[0] == 0xff) return "Not Set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 417: {
        if (data_size >= 1 && data[0] == 0x00) return "White Rice";
        if (data_size >= 1 && data[0] == 0x01) return "Brown Rice";
        if (data_size >= 1 && data[0] == 0x02) return "Asparagus";
        if (data_size >= 1 && data[0] == 0x03) return "Broccoli";
        if (data_size >= 1 && data[0] == 0x04) return "Brussels Sprouts";
        if (data_size >= 1 && data[0] == 0x05) return "Carrots";
        if (data_size >= 1 && data[0] == 0x06) return "Cauliflower";
        if (data_size >= 1 && data[0] == 0x07) return "Zucchini";
        if (data_size >= 1 && data[0] == 0x08) return "Squash";
        if (data_size >= 1 && data[0] == 0x09) return "Green Beans";
        if (data_size >= 1 && data[0] == 0x0a) return "Quinoa";
        if (data_size >= 1 && data[0] == 0x0b) return "Potatoes";
        if (data_size >= 1 && data[0] == 0x0c) return "Shrimp";
        if (data_size >= 1 && data[0] == 0x0d) return "Chicken Breast";
        if (data_size >= 1 && data[0] == 0x0e) return "Fish";
        if (data_size >= 1 && data[0] == 0x0f) return "Bay Scallops";
        if (data_size >= 1 && data[0] == 0x10) return "Sea Scallops";
        if (data_size >= 1 && data[0] == 0x11) return "Sensed White Rice";
        if (data_size >= 1 && data[0] == 0x12) return "Sensed Asparagus";
        if (data_size >= 1 && data[0] == 0x13) return "Sensed Brussels Sprouts";
        if (data_size >= 1 && data[0] == 0x14) return "Sensed Carrots";
        if (data_size >= 1 && data[0] == 0x15) return "Sensed Cauliflower";
        if (data_size >= 1 && data[0] == 0x16) return "Sensed Squash";
        if (data_size >= 1 && data[0] == 0x17) return "Sensed Potatoes";
        if (data_size >= 1 && data[0] == 0x18) return "Sensed Zucchini";
        if (data_size >= 1 && data[0] == 0xff) return "not set";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 418: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop";
        if (data_size >= 1 && data[0] == 0x01) return "Start";
        if (data_size >= 1 && data[0] == 0x03) return "Pause";
        if (data_size >= 1 && data[0] == 0x04) return "Resume";
        if (data_size >= 1 && data[0] == 0x05) return "Upload";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 419: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 420: {
        if (data_size >= 1 && data[0] == 0x00) return "Stop";
        if (data_size >= 1 && data[0] == 0x01) return "Heat";
        if (data_size >= 1 && data[0] == 0x02) return "Fan";
        if (data_size >= 1 && data[0] == 0x03) return "Cool";
        if (data_size >= 1 && data[0] == 0x04) return "Parameter Set";
        if (data_size >= 1 && data[0] == 0x05) return "Forced Run";
        if (data_size >= 1 && data[0] == 0x06) return "External Thermostat";
        if (data_size >= 1 && data[0] == 0x07) return "Fault Code Display";
        if (data_size >= 1 && data[0] == 0x08) return "Factory Test";
        if (data_size >= 1 && data[0] == 0x09) return "Engineering Digit Entry";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 421: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 422: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 423: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 424: {
        if (data_size >= 1 && data[0] == 0x00) return "Automatic";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "Low";
        if (data_size >= 1 && data[0] == 0x03) return "Idle";
        if (data_size >= 1 && data[0] == 0x04) return "User Level";
        if (data_size >= 1 && data[0] == 0x05) return "Off";
        if (data_size >= 1 && data[0] == 0x06) return "Lowest Speed";
        if (data_size >= 1 && data[0] == 0x07) return "Dehumidify";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 425: {
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "Low";
        if (data_size >= 1 && data[0] == 0x03) return "Idle";
        if (data_size >= 1 && data[0] == 0x04) return "User Level";
        if (data_size >= 1 && data[0] == 0x05) return "Off";
        if (data_size >= 1 && data[0] == 0x06) return "Lowest Speed";
        if (data_size >= 1 && data[0] == 0x07) return "Dehumidify";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 426: {
        if (data_size >= 1 && data[0] == 0x00) return "Automatic";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        if (data_size >= 1 && data[0] == 0x02) return "Low";
        if (data_size >= 1 && data[0] == 0x03) return "Idle";
        if (data_size >= 1 && data[0] == 0x04) return "User Level";
        if (data_size >= 1 && data[0] == 0x05) return "Off";
        if (data_size >= 1 && data[0] == 0x06) return "Lowest Speed";
        if (data_size >= 1 && data[0] == 0x07) return "Dehumidify";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 427: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 428: {
        if (data_size >= 1 && data[0] == 0x00) return "None";
        if (data_size >= 1 && data[0] == 0x01) return "Simple";
        if (data_size >= 1 && data[0] == 0x02) return "Standard";
        if (data_size >= 1 && data[0] == 0x03) return "Advanced";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 429: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 430: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Standard";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 431: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 432: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 433: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 434: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 435: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 436: {
        if (data_size >= 1 && data[0] == 0x00) return "Set from 60F to 85F";
        if (data_size >= 1 && data[0] == 0x01) return "Set from 64F to 85F";
        if (data_size >= 1 && data[0] == 0x02) return "Set from 66F to 85F";
        if (data_size >= 1 && data[0] == 0x03) return "Set from 68F to 85F";
        if (data_size >= 1 && data[0] == 0x04) return "Set from 70F to 85F";
        if (data_size >= 1 && data[0] == 0x05) return "Set from 72F to 85F";
        if (data_size >= 1 && data[0] == 0x06) return "Set from 74F to 85F";
        if (data_size >= 1 && data[0] == 0x07) return "Set from 76F to 85F";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 437: {
        if (data_size >= 1 && data[0] == 0x00) return "Set from 60F to 65F";
        if (data_size >= 1 && data[0] == 0x01) return "Set from 60F to 70F";
        if (data_size >= 1 && data[0] == 0x02) return "Set from 60F to 72F";
        if (data_size >= 1 && data[0] == 0x03) return "Set from 60F to 74F";
        if (data_size >= 1 && data[0] == 0x04) return "Set from 60F to 76F";
        if (data_size >= 1 && data[0] == 0x05) return "Set from 60F to 78F";
        if (data_size >= 1 && data[0] == 0x06) return "Set from 60F to 80F";
        if (data_size >= 1 && data[0] == 0x07) return "Set from 60F to 85F";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 438: {
        if (data_size >= 1 && data[0] == 0x00) return "Inactive";
        if (data_size >= 1 && data[0] == 0x01) return "Active";
        if (data_size >= 1 && data[0] == 0x02) return "Auto Changeover";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 439: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 440: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 441: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 442: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Speed 1";
        if (data_size >= 1 && data[0] == 0x02) return "Speed 2";
        if (data_size >= 1 && data[0] == 0x03) return "Speed 3";
        if (data_size >= 1 && data[0] == 0x04) return "Speed 4";
        if (data_size >= 1 && data[0] == 0x05) return "Speed 5";
        if (data_size >= 1 && data[0] == 0x06) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 443: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 444: {
        if (data_size >= 1 && data[0] == 0x01) return "Revision 1";
        if (data_size >= 1 && data[0] == 0x02) return "Revision 2";
        if (data_size >= 1 && data[0] == 0x03) return "Revision 3";
        if (data_size >= 1 && data[0] == 0x04) return "Revision 4";
        if (data_size >= 1 && data[0] == 0x05) return "Revision 5";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 445: {
        if (data_size >= 1 && data[0] == 0x00) return "Cyclic";
        if (data_size >= 1 && data[0] == 0x01) return "Continuous";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 446: {
        if (data_size >= 1 && data[0] == 0x00) return "Cyclic";
        if (data_size >= 1 && data[0] == 0x01) return "Continuous";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 447: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 448: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 449: {
        if (data_size >= 1 && data[0] == 0x00) return "Disabled";
        if (data_size >= 1 && data[0] == 0x01) return "Enabled";
        if (data_size >= 1 && data[0] == 0x02) return "Low";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 450: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 451: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 452: {
        if (data_size >= 1 && data[0] == 0x00) return "Electric Heat Only";
        if (data_size >= 1 && data[0] == 0x01) return "High Demand";
        if (data_size >= 1 && data[0] == 0x02) return "Hybrid";
        if (data_size >= 1 && data[0] == 0x03) return "Boost Heat Pump Allowed";
        if (data_size >= 1 && data[0] == 0x04) return "Heat Pump Only";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 453: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 454: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 455: {
        if (data_size >= 1 && data[0] == 0x00) return "No Filter";
        if (data_size >= 1 && data[0] == 0x01) return "MERV 13 Filter";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 456: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 457: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Standard";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 458: {
        if (data_size >= 1 && data[0] == 0x00) return "Central Desk Control";
        if (data_size >= 1 && data[0] == 0x01) return "Occupancy Control";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 459: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 460: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 461: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 462: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 463: {
        if (data_size >= 1 && data[0] == 0x00) return "Auto";
        if (data_size >= 1 && data[0] == 0x01) return "Heat";
        if (data_size >= 1 && data[0] == 0x02) return "Fan";
        if (data_size >= 1 && data[0] == 0x03) return "Cool";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 464: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 465: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 466: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 467: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 468: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 469: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 470: {
        if (data_size >= 1 && data[0] == 0x00) return "Low";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 471: {
        if (data_size >= 1 && data[0] == 0x00) return "Low";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 472: {
        if (data_size >= 1 && data[0] == 0x00) return "Low";
        if (data_size >= 1 && data[0] == 0x01) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 473: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 474: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 475: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 476: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 477: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 478: {
        if (data_size >= 1 && data[0] == 0x00) return "Normal";
        if (data_size >= 1 && data[0] == 0x01) return "Turbo";
        if (data_size >= 1 && data[0] == 0x02) return "Quiet";
        if (data_size >= 1 && data[0] == 0xff) return "Not Applicable";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 479: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 480: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 481: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 482: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 483: {
        if (data_size >= 1 && data[0] == 0x00) return "No Request";
        if (data_size >= 1 && data[0] == 0x01) return "Request Self Clean Cycle To Occur";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 484: {
        if (data_size >= 1 && data[0] == 0x00) return "250 Hours";
        if (data_size >= 1 && data[0] == 0x01) return "500 Hours";
        if (data_size >= 1 && data[0] == 0x02) return "1000 Hours";
        if (data_size >= 1 && data[0] == 0x03) return "1500 Hours";
        if (data_size >= 1 && data[0] == 0x04) return "Never";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 485: {
        if (data_size >= 1 && data[0] == 0x00) return "Set Temperature";
        if (data_size >= 1 && data[0] == 0x01) return "Ambient Temperature";
        if (data_size >= 1 && data[0] == 0xfe) return "Error";
        if (data_size >= 1 && data[0] == 0xff) return "Not Available";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 486: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 487: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 488: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 489: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 490: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 491: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 492: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 493: {
        if (data_size >= 1 && data[0] == 0x00) return "Auto";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x02) return "Circulate";
        if (data_size >= 1 && data[0] == 0xff) return "Fan Not Currently Available";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 494: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 495: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 496: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 497: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 498: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 499: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 500: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 501: {
        if (data_size >= 1 && data[0] == 0x01) return "Auto";
        if (data_size >= 1 && data[0] == 0x02) return "Low";
        if (data_size >= 1 && data[0] == 0x04) return "Medium";
        if (data_size >= 1 && data[0] == 0x08) return "High";
        if (data_size >= 1 && data[0] == 0x10) return "Smart Dry";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 502: {
        if (data_size >= 1 && data[0] == 0x00) return "Cool";
        if (data_size >= 1 && data[0] == 0x01) return "Fan Only";
        if (data_size >= 1 && data[0] == 0x02) return "Energy Saver";
        if (data_size >= 1 && data[0] == 0x03) return "Heat";
        if (data_size >= 1 && data[0] == 0x04) return "Dehumidify/Dry";
        if (data_size >= 1 && data[0] == 0x05) return "Auto";
        if (data_size >= 1 && data[0] == 0x06) return "Turbo Cool";
        if (data_size >= 1 && data[0] == 0x07) return "Silent";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 503: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 504: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 505: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 506: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 507: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Auto";
        if (data_size >= 1 && data[0] == 0x02) return "Undefined";
        if (data_size >= 1 && data[0] == 0x03) return "Cool";
        if (data_size >= 1 && data[0] == 0x04) return "Heat";
        if (data_size >= 1 && data[0] == 0x05) return "Emergency Heat";
        if (data_size >= 1 && data[0] == 0x06) return "Precooling";
        if (data_size >= 1 && data[0] == 0x07) return "Fan only";
        if (data_size >= 1 && data[0] == 0x08) return "Dry";
        if (data_size >= 1 && data[0] == 0x09) return "Sleep";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 508: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "Low";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "High";
        if (data_size >= 1 && data[0] == 0x04) return "On";
        if (data_size >= 1 && data[0] == 0x05) return "Auto";
        if (data_size >= 1 && data[0] == 0x06) return "Smart";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 509: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 510: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 511: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 512: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 513: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 514: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 515: {
        if (data_size >= 1 && data[0] == 0x00) return "Not Installed";
        if (data_size >= 1 && data[0] == 0x01) return "Installed";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 516: {
        if (data_size >= 1 && data[0] == 0x00) return "Open";
        if (data_size >= 1 && data[0] == 0x01) return "Closed";
        if (data_size >= 1 && data[0] == 0x02) return "Transition";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 517: {
        if (data_size >= 1 && data[0] == 0x00) return "Open";
        if (data_size >= 1 && data[0] == 0x01) return "Closed";
        if (data_size >= 1 && data[0] == 0x02) return "Transition (written only by application)";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 518: {
        if (data_size >= 1 && data[0] == 0x00) return "Reserved";
        if (data_size >= 1 && data[0] == 0x01) return "Light";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "Dark";
        if (data_size >= 1 && data[0] == 0x04) return "Gold";
        if (data_size >= 1 && data[0] == 0x05) return "Iced Coffee";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 519: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 520: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 521: {
        if (data_size >= 1 && data[0] == 0x00) return "Custom";
        if (data_size >= 1 && data[0] == 0x01) return "Light";
        if (data_size >= 1 && data[0] == 0x02) return "Medium";
        if (data_size >= 1 && data[0] == 0x03) return "Dark";
        if (data_size >= 1 && data[0] == 0x04) return "Gold";
        if (data_size >= 1 && data[0] == 0x05) return "Iced Coffee";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 522: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 523: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 524: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 525: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 526: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 527: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 528: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 529: {
        if (data_size >= 1 && data[0] == 0x00) return "Espresso";
        if (data_size >= 1 && data[0] == 0x01) return "Americano";
        if (data_size >= 1 && data[0] == 0x02) return "Lungo";
        if (data_size >= 1 && data[0] == 0x03) return "Hot Water";
        if (data_size >= 1 && data[0] == 0x04) return "Steam";
        if (data_size >= 1 && data[0] == 0x05) return "Ristretto";
        if (data_size >= 1 && data[0] == 0x06) return "Doppio";
        if (data_size >= 1 && data[0] == 0x07) return "Triple Shot";
        if (data_size >= 1 && data[0] == 0x08) return "Red Eye";
        if (data_size >= 1 && data[0] == 0x09) return "MyBrew/MyCup";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 530: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 531: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 532: {
        if (data_size >= 1 && data[0] == 0x00) return "Very Soft";
        if (data_size >= 1 && data[0] == 0x01) return "Soft";
        if (data_size >= 1 && data[0] == 0x02) return "Hard";
        if (data_size >= 1 && data[0] == 0x03) return "Very Hard";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 533: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 534: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 535: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 536: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 537: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 538: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 539: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 540: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 541: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 542: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 543: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 544: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 545: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 546: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 547: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 548: {
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x02) return "Dim";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 549: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 550: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 551: {
        if (data_size >= 1 && data[0] == 0x00) return "Small";
        if (data_size >= 1 && data[0] == 0x01) return "Standard";
        if (data_size >= 1 && data[0] == 0x02) return "Large";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 552: {
        if (data_size >= 1 && data[0] == 0x00) return "Normal";
        if (data_size >= 1 && data[0] == 0x01) return "More Frequent";
        if (data_size >= 1 && data[0] == 0x02) return "Very Frequent";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 553: {
        if (data_size >= 1 && data[0] == 0x00) return "Normal";
        if (data_size >= 1 && data[0] == 0x01) return "Longer";
        if (data_size >= 1 && data[0] == 0x02) return "Longest";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 554: {
        if (data_size >= 1 && data[0] == 0x00) return "Unknown";
        if (data_size >= 1 && data[0] == 0x01) return "Carbon Filter";
        if (data_size >= 1 && data[0] == 0x02) return "Scale Inhibiting Filter";
        if (data_size >= 1 && data[0] == 0x03) return "Opal Mini: CTO";
        if (data_size >= 1 && data[0] == 0x04) return "Opal Mini: CTO + Scale Inhibiting";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 555: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 556: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 557: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 558: {
        if (data_size >= 1 && data[0] == 0x00) return "Low";
        if (data_size >= 1 && data[0] == 0x01) return "Medium";
        if (data_size >= 1 && data[0] == 0x02) return "High";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 559: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 560: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 561: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 562: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 563: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 564: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 565: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 566: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 567: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 568: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 569: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 570: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 571: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 572: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 573: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 574: {
        if (data_size >= 1 && data[0] == 0x00) return "Air Fry";
        if (data_size >= 1 && data[0] == 0x01) return "Bake";
        if (data_size >= 1 && data[0] == 0x02) return "Broil";
        if (data_size >= 1 && data[0] == 0x03) return "Roast";
        if (data_size >= 1 && data[0] == 0x04) return "Reheat";
        if (data_size >= 1 && data[0] == 0x05) return "Warm";
        if (data_size >= 1 && data[0] == 0x06) return "Slow Cook";
        if (data_size >= 1 && data[0] == 0x07) return "Dehydrate";
        if (data_size >= 1 && data[0] == 0x08) return "Proof";
        if (data_size >= 1 && data[0] == 0x09) return "Cookie";
        if (data_size >= 1 && data[0] == 0x0a) return "Pizza";
        if (data_size >= 1 && data[0] == 0x0b) return "Bagel";
        if (data_size >= 1 && data[0] == 0x0c) return "Toast";
        if (data_size >= 1 && data[0] == 0x0d) return "Crisp Finish";
        if (data_size >= 1 && data[0] == 0x0e) return "Cake";
        if (data_size >= 1 && data[0] == 0x0f) return "Cookie with Preferences";
        if (data_size >= 1 && data[0] == 0x10) return "Pizza with Preferences";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 575: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 576: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 577: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 578: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 579: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 580: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 581: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 582: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 583: {
        if (data_size >= 1 && data[0] == 0x00) return "Subtract Cycle Time";
        if (data_size >= 1 && data[0] == 0x01) return "Add Cycle Time";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 584: {
        if (data_size >= 1 && data[0] == 0x00) return "Metric";
        if (data_size >= 1 && data[0] == 0x01) return "US Standard";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 585: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 586: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 587: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 588: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 589: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 590: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 591: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 592: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 593: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 594: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 595: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 596: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 597: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 598: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 599: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 600: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 601: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 602: {
        if (data_size >= 1 && data[0] == 0x00) return "No Reminder";
        if (data_size >= 1 && data[0] == 0x01) return "1 hour Interval";
        if (data_size >= 1 && data[0] == 0x02) return "2 hour Interval";
        if (data_size >= 1 && data[0] == 0x03) return "3 hour Interval";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 603: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 604: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 605: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 606: {
        if (data_size >= 1 && data[0] == 0x00) return "All";
        if (data_size >= 1 && data[0] == 0x01) return "Custom";
        if (data_size >= 1 && data[0] == 0x02) return "Keep Warm";
        if (data_size >= 1 && data[0] == 0x03) return "Brisket";
        if (data_size >= 1 && data[0] == 0x04) return "Pork Rib";
        if (data_size >= 1 && data[0] == 0x05) return "Pork Butt";
        if (data_size >= 1 && data[0] == 0x06) return "Wings";
        if (data_size >= 1 && data[0] == 0x07) return "Chicken";
        if (data_size >= 1 && data[0] == 0x08) return "Salmon";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 607: {
        if (data_size >= 1 && data[0] == 0x01) return "On";
        if (data_size >= 1 && data[0] == 0x00) return "Off";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 608: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 609: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 610: {
        if (data_size >= 1 && data[0] == 0x00) return "Not in demand response mode";
        if (data_size >= 1 && data[0] == 0x01) return "In Demand Response mode";
        if (data_size >= 1 && data[0] == 0x02) return "In Temporary Load Reduction mode";
        if (data_size >= 1 && data[0] == 0x03) return "In appliances load reduction mode (Option 3 is controlled by the appliance and it cannot be triggered by the cloud)";
        if (data_size >= 1 && data[0] == 0x04) return "In Temporary Load Increase mode";
        if (data_size >= 1 && data[0] == 0x05) return "In Temperature Offset mode";
        if (data_size >= 1 && data[0] == 0xff) return "Invalid";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 611: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 612: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 613: {
        if (data_size >= 1 && data[0] == 0x00) return "None";
        if (data_size >= 1 && data[0] == 0x01) return "Time Of Use";
        if (data_size >= 1 && data[0] == 0x02) return "Block";
        if (data_size >= 1 && data[0] == 0x03) return "TierBlock";
        if (data_size >= 1 && data[0] == 0x04) return "BlockTier";
        snprintf(result, sizeof(result), "0x%02x", data[0]); return result;
      }
      case 614: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 615: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 616: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 617: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 618: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 619: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 620: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      case 621: {
        if (data_size >= 2) snprintf(result, sizeof(result), "%u", (data[0] << 8) | data[1]); else snprintf(result, sizeof(result), "%u", data[0]); return result;
      }
      default:
        snprintf(result, sizeof(result), "unknown"); return result;
      }
    }
  }
  return NULL;
}

#ifdef __cplusplus
}
#endif