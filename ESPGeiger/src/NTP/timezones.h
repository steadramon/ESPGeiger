/*
  timezones.h - Timezone objects for NTPs
  
  Copyright (C) 2023 @steadramon

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#pragma once
#ifndef TIMEZONES_H
#define TIMEZONES_H

static const char TZ_0[] PROGMEM = "GMT0";
static const char TZ_1[] PROGMEM = "EAT-3";
static const char TZ_2[] PROGMEM = "CET-1";
static const char TZ_3[] PROGMEM = "WAT-1";
static const char TZ_4[] PROGMEM = "CAT-2";
static const char TZ_5[] PROGMEM = "EET-2";
static const char TZ_6[] PROGMEM = "<+01>-1";
static const char TZ_7[] PROGMEM = "CET-1CEST,M3.5.0,M10.5.0/3";
static const char TZ_8[] PROGMEM = "SAST-2";
static const char TZ_9[] PROGMEM = "HST10HDT,M3.2.0,M11.1.0";
static const char TZ_10[] PROGMEM = "AKST9AKDT,M3.2.0,M11.1.0";
static const char TZ_11[] PROGMEM = "AST4";
static const char TZ_12[] PROGMEM = "<-03>3";
static const char TZ_13[] PROGMEM = "<-04>4<-03>,M10.1.0/0,M3.4.0/0";
static const char TZ_14[] PROGMEM = "EST5";
static const char TZ_15[] PROGMEM = "CST6CDT,M4.1.0,M10.5.0";
static const char TZ_16[] PROGMEM = "CST6";
static const char TZ_17[] PROGMEM = "<-04>4";
static const char TZ_18[] PROGMEM = "<-05>5";
static const char TZ_19[] PROGMEM = "MST7MDT,M3.2.0,M11.1.0";
static const char TZ_20[] PROGMEM = "CST6CDT,M3.2.0,M11.1.0";
static const char TZ_21[] PROGMEM = "MST7MDT,M4.1.0,M10.5.0";
static const char TZ_22[] PROGMEM = "MST7";
static const char TZ_23[] PROGMEM = "EST5EDT,M3.2.0,M11.1.0";
static const char TZ_24[] PROGMEM = "PST8PDT,M3.2.0,M11.1.0";
static const char TZ_25[] PROGMEM = "AST4ADT,M3.2.0,M11.1.0";
static const char TZ_26[] PROGMEM = "<-03>3<-02>,M3.5.0/-2,M10.5.0/-1";
static const char TZ_27[] PROGMEM = "CST5CDT,M3.2.0/0,M11.1.0/1";
static const char TZ_28[] PROGMEM = "<-03>3<-02>,M3.2.0,M11.1.0";
static const char TZ_29[] PROGMEM = "<-02>2";
static const char TZ_30[] PROGMEM = "<-04>4<-03>,M9.1.6/24,M4.1.6/24";
static const char TZ_31[] PROGMEM = "<-01>1<+00>,M3.5.0/0,M10.5.0/1";
static const char TZ_32[] PROGMEM = "NST3:30NDT,M3.2.0,M11.1.0";
static const char TZ_33[] PROGMEM = "<+11>-11";
static const char TZ_34[] PROGMEM = "<+07>-7";
static const char TZ_35[] PROGMEM = "<+10>-10";
static const char TZ_36[] PROGMEM = "AEST-10AEDT,M10.1.0,M4.1.0/3";
static const char TZ_37[] PROGMEM = "<+05>-5";
static const char TZ_38[] PROGMEM = "NZST-12NZDT,M9.5.0,M4.1.0/3";
static const char TZ_39[] PROGMEM = "<+03>-3";
static const char TZ_40[] PROGMEM = "<+00>0<+02>-2,M3.5.0/1,M10.5.0/3";
static const char TZ_41[] PROGMEM = "<+06>-6";
static const char TZ_42[] PROGMEM = "EET-2EEST,M3.5.4/24,M10.5.5/1";
static const char TZ_43[] PROGMEM = "<+12>-12";
static const char TZ_44[] PROGMEM = "<+04>-4";
static const char TZ_45[] PROGMEM = "EET-2EEST,M3.5.0/0,M10.5.0/0";
static const char TZ_46[] PROGMEM = "<+08>-8";
static const char TZ_47[] PROGMEM = "IST-5:30";
static const char TZ_48[] PROGMEM = "<+09>-9";
static const char TZ_49[] PROGMEM = "CST-8";
static const char TZ_50[] PROGMEM = "<+0530>-5:30";
static const char TZ_51[] PROGMEM = "EET-2EEST,M3.5.5/0,M10.5.5/0";
static const char TZ_52[] PROGMEM = "EET-2EEST,M3.5.0/3,M10.5.0/4";
static const char TZ_53[] PROGMEM = "EET-2EEST,M3.4.4/48,M10.4.4/49";
static const char TZ_54[] PROGMEM = "HKT-8";
static const char TZ_55[] PROGMEM = "WIB-7";
static const char TZ_56[] PROGMEM = "WIT-9";
static const char TZ_57[] PROGMEM = "IST-2IDT,M3.4.4/26,M10.5.0";
static const char TZ_58[] PROGMEM = "<+0430>-4:30";
static const char TZ_59[] PROGMEM = "PKT-5";
static const char TZ_60[] PROGMEM = "<+0545>-5:45";
static const char TZ_61[] PROGMEM = "WITA-8";
static const char TZ_62[] PROGMEM = "PST-8";
static const char TZ_63[] PROGMEM = "KST-9";
static const char TZ_64[] PROGMEM = "<+0630>-6:30";
static const char TZ_65[] PROGMEM = "<+0330>-3:30<+0430>,J79/24,J263/24";
static const char TZ_66[] PROGMEM = "JST-9";
static const char TZ_67[] PROGMEM = "WET0WEST,M3.5.0/1,M10.5.0";
static const char TZ_68[] PROGMEM = "<-01>1";
static const char TZ_69[] PROGMEM = "ACST-9:30ACDT,M10.1.0,M4.1.0/3";
static const char TZ_70[] PROGMEM = "AEST-10";
static const char TZ_71[] PROGMEM = "ACST-9:30";
static const char TZ_72[] PROGMEM = "<+0845>-8:45";
static const char TZ_73[] PROGMEM = "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0";
static const char TZ_74[] PROGMEM = "AWST-8";
static const char TZ_75[] PROGMEM = "<-06>6<-05>,M9.1.6/22,M4.1.6/22";
static const char TZ_76[] PROGMEM = "IST-1GMT0,M10.5.0,M3.5.0/1";
static const char TZ_77[] PROGMEM = "<-10>10";
static const char TZ_78[] PROGMEM = "<-11>11";
static const char TZ_79[] PROGMEM = "<-12>12";
static const char TZ_80[] PROGMEM = "<-06>6";
static const char TZ_81[] PROGMEM = "<-07>7";
static const char TZ_82[] PROGMEM = "<-08>8";
static const char TZ_83[] PROGMEM = "<-09>9";
static const char TZ_84[] PROGMEM = "<+13>-13";
static const char TZ_85[] PROGMEM = "<+14>-14";
static const char TZ_86[] PROGMEM = "<+02>-2";
static const char TZ_87[] PROGMEM = "UTC0";
static const char TZ_88[] PROGMEM = "GMT0BST,M3.5.0/1,M10.5.0";
static const char TZ_89[] PROGMEM = "EET-2EEST,M3.5.0,M10.5.0/3";
static const char TZ_90[] PROGMEM = "MSK-3";
static const char TZ_91[] PROGMEM = "<-00>0";
static const char TZ_92[] PROGMEM = "HST10";
static const char TZ_93[] PROGMEM = "MET-1MEST,M3.5.0,M10.5.0/3";
static const char TZ_94[] PROGMEM = "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45";
static const char TZ_95[] PROGMEM = "<+13>-13<+14>,M9.5.0/3,M4.1.0/4";
static const char TZ_96[] PROGMEM = "<+12>-12<+13>,M11.2.0,M1.2.3/99";
static const char TZ_97[] PROGMEM = "ChST-10";
static const char TZ_98[] PROGMEM = "<-0930>9:30";
static const char TZ_99[] PROGMEM = "SST11";
static const char TZ_100[] PROGMEM = "<+11>-11<+12>,M10.1.0,M4.1.0/3";

static const char *posix[] PROGMEM = {
    /*   0 */ TZ_0,
    /*   1 */ TZ_1,
    /*   2 */ TZ_2,
    /*   3 */ TZ_3,
    /*   4 */ TZ_4,
    /*   5 */ TZ_5,
    /*   6 */ TZ_6,
    /*   7 */ TZ_7,
    /*   8 */ TZ_8,
    /*   9 */ TZ_9,
    /*  10 */ TZ_10,
    /*  11 */ TZ_11,
    /*  12 */ TZ_12,
    /*  13 */ TZ_13,
    /*  14 */ TZ_14,
    /*  15 */ TZ_15,
    /*  16 */ TZ_16,
    /*  17 */ TZ_17,
    /*  18 */ TZ_18,
    /*  19 */ TZ_19,
    /*  20 */ TZ_20,
    /*  21 */ TZ_21,
    /*  22 */ TZ_22,
    /*  23 */ TZ_23,
    /*  24 */ TZ_24,
    /*  25 */ TZ_25,
    /*  26 */ TZ_26,
    /*  27 */ TZ_27,
    /*  28 */ TZ_28,
    /*  29 */ TZ_29,
    /*  30 */ TZ_30,
    /*  31 */ TZ_31,
    /*  32 */ TZ_32,
    /*  33 */ TZ_33,
    /*  34 */ TZ_34,
    /*  35 */ TZ_35,
    /*  36 */ TZ_36,
    /*  37 */ TZ_37,
    /*  38 */ TZ_38,
    /*  39 */ TZ_39,
    /*  40 */ TZ_40,
    /*  41 */ TZ_41,
    /*  42 */ TZ_42,
    /*  43 */ TZ_43,
    /*  44 */ TZ_44,
    /*  45 */ TZ_45,
    /*  46 */ TZ_46,
    /*  47 */ TZ_47,
    /*  48 */ TZ_48,
    /*  49 */ TZ_49,
    /*  50 */ TZ_50,
    /*  51 */ TZ_51,
    /*  52 */ TZ_52,
    /*  53 */ TZ_53,
    /*  54 */ TZ_54,
    /*  55 */ TZ_55,
    /*  56 */ TZ_56,
    /*  57 */ TZ_57,
    /*  58 */ TZ_58,
    /*  59 */ TZ_59,
    /*  60 */ TZ_60,
    /*  61 */ TZ_61,
    /*  62 */ TZ_62,
    /*  63 */ TZ_63,
    /*  64 */ TZ_64,
    /*  65 */ TZ_65,
    /*  66 */ TZ_66,
    /*  67 */ TZ_67,
    /*  68 */ TZ_68,
    /*  69 */ TZ_69,
    /*  70 */ TZ_70,
    /*  71 */ TZ_71,
    /*  72 */ TZ_72,
    /*  73 */ TZ_73,
    /*  74 */ TZ_74,
    /*  75 */ TZ_75,
    /*  76 */ TZ_76,
    /*  77 */ TZ_77,
    /*  78 */ TZ_78,
    /*  79 */ TZ_79,
    /*  80 */ TZ_80,
    /*  81 */ TZ_81,
    /*  82 */ TZ_82,
    /*  83 */ TZ_83,
    /*  84 */ TZ_84,
    /*  85 */ TZ_85,
    /*  86 */ TZ_86,
    /*  87 */ TZ_87,
    /*  88 */ TZ_88,
    /*  89 */ TZ_89,
    /*  90 */ TZ_90,
    /*  91 */ TZ_91,
    /*  92 */ TZ_92,
    /*  93 */ TZ_93,
    /*  94 */ TZ_94,
    /*  95 */ TZ_95,
    /*  96 */ TZ_96,
    /*  97 */ TZ_97,
    /*  98 */ TZ_98,
    /*  99 */ TZ_99,
    /* 100 */ TZ_100,
};

#ifdef TZ_ETC_ONLY

#else
PROGMEM static const struct TZoneH {
  uint32_t hash : 24;
  uint8_t posix : 8;
} zones[] = {
    {3052, 0},       // Etc/localtime
    {3177, 12},      // America/Argentina/Buenos_Aires
    {5424, 77},      // Pacific/Rarotonga
    {6596, 29},      // Brazil/DeNoronha
    {8404, 99},      // Pacific/Midway
    {16206, 88},     // Europe/London
    {23764, 7},      // Europe/Berlin
    {25742, 70},     // Australia/Lindeman
    {28550, 52},     // Europe/Nicosia
    {33699, 41},     // Asia/Urumqi
    {34704, 88},     // Europe/Jersey
    {39895, 97},     // Pacific/Saipan
    {43289, 19},     // US/Mountain
    {43535, 43},     // Pacific/Kwajalein
    {45558, 17},     // America/Campo_Grande
    {47414, 54},     // Hongkong
    {56581, 16},     // America/Guatemala
    {57398, 78},     // Pacific/Niue
    {61482, 53},     // Asia/Hebron
    {63824, 22},     // America/Hermosillo
    {77667, 35},     // Asia/Ust-Nera
    {80762, 18},     // America/Guayaquil
    {92809, 12},     // America/Sao_Paulo
    {97827, 52},     // Europe/Bucharest
    {98189, 24},     // Mexico/BajaNorte
    {100383, 45},    // Asia/Beirut
    {104536, 48},    // Asia/Dili
    {109229, 43},    // Asia/Kamchatka
    {110344, 5},     // Egypt
    {121092, 12},    // Atlantic/Stanley
    {136540, 90},    // Europe/Moscow
    {153557, 55},    // Asia/Pontianak
    {156843, 52},    // Europe/Kiev
    {160344, 37},    // Asia/Yekaterinburg
    {161461, 65},    // Iran
    {165199, 20},    // America/Resolute
    {166924, 33},    // Pacific/Guadalcanal
    {186333, 7},     // Europe/Rome
    {190474, 87},    // Universal
    {191305, 93},    // MET
    {195711, 97},    // Pacific/Guam
    {196047, 92},    // US/Hawaii
    {201945, 52},    // Europe/Athens
    {208700, 48},    // Asia/Yakutsk
    {210977, 49},    // Asia/Macau
    {226052, 16},    // America/Costa_Rica
    {232294, 33},    // Pacific/Bougainville
    {241713, 40},    // Antarctica/Troll
    {245936, 7},     // Europe/Podgorica
    {250998, 36},    // Australia/NSW
    {251033, 23},    // EST5EDT
    {267738, 34},    // Asia/Tomsk
    {270075, 23},    // America/Iqaluit
    {274113, 29},    // America/Noronha
    {276475, 7},     // Europe/Tirane
    {277285, 84},    // Pacific/Enderbury
    {279399, 61},    // Asia/Makassar
    {281938, 19},    // America/Boise
    {285314, 88},    // Europe/Guernsey
    {288178, 94},    // NZ-CHAT
    {288555, 73},    // Australia/Lord_Howe
    {289636, 25},    // America/Moncton
    {290785, 12},    // America/Punta_Arenas
    {296680, 62},    // Asia/Manila
    {300125, 20},    // Canada/Central
    {303455, 41},    // Asia/Bishkek
    {307480, 37},    // Asia/Aqtau
    {310155, 12},    // America/Argentina/San_Juan
    {318936, 85},    // Etc/GMT-14
    {320548, 35},    // Etc/GMT-10
    {320951, 33},    // Etc/GMT-11
    {321354, 43},    // Etc/GMT-12
    {321757, 84},    // Etc/GMT-13
    {322216, 36},    // Australia/Currie
    {329953, 15},    // Mexico/General
    {335595, 21},    // Mexico/BajaSur
    {346512, 49},    // Asia/Taipei
    {362265, 90},    // W-SU
    {367282, 12},    // America/Argentina/Ushuaia
    {375886, 12},    // America/Argentina/Mendoza
    {379069, 34},    // Asia/Bangkok
    {392475, 12},    // America/Argentina/Tucuman
    {398176, 36},    // Australia/Tasmania
    {402710, 43},    // Pacific/Nauru
    {407955, 44},    // Europe/Samara
    {419965, 16},    // America/Swift_Current
    {421329, 38},    // NZ
    {427478, 10},    // America/Juneau
    {431737, 19},    // America/Cambridge_Bay
    {431765, 7},     // Europe/Madrid
    {445829, 0},     // Iceland
    {448725, 6},     // Africa/Casablanca
    {456379, 60},    // Asia/Kathmandu
    {457803, 24},    // America/Vancouver
    {461663, 15},    // America/Monterrey
    {467586, 67},    // Atlantic/Canary
    {492113, 88},    // Europe/Belfast
    {492850, 88},    // Europe/Isle_of_Man
    {498759, 22},    // America/Dawson_Creek
    {501621, 12},    // America/Cayenne
    {512398, 0},     // Etc/GMT0
    {513033, 12},    // Antarctica/Palmer
    {513738, 37},    // Asia/Samarkand
    {516616, 99},    // Pacific/Samoa
    {520109, 91},    // Factory
    {525358, 41},    // Asia/Omsk
    {526318, 89},    // Europe/Chisinau
    {529695, 63},    // ROK
    {529783, 15},    // America/Mexico_City
    {532919, 49},    // ROC
    {537346, 21},    // America/Chihuahua
    {556011, 2},     // Africa/Tunis
    {570340, 10},    // America/Anchorage
    {573129, 44},    // Indian/Reunion
    {583325, 35},    // Pacific/Yap
    {587591, 10},    // America/Yakutat
    {595614, 20},    // America/Rankin_Inlet
    {601710, 1},     // Indian/Comoro
    {604958, 36},    // Australia/ACT
    {609925, 57},    // Israel
    {611845, 7},     // Poland
    {614930, 0},     // GMT-0
    {626509, 59},    // Asia/Karachi
    {627717, 98},    // Pacific/Marquesas
    {639134, 11},    // America/Barbados
    {639738, 33},    // Pacific/Noumea
    {648563, 0},     // Africa/Abidjan
    {653866, 77},    // Pacific/Tahiti
    {657734, 1},     // Africa/Nairobi
    {661560, 46},    // Asia/Ulaanbaatar
    {663785, 33},    // Asia/Sakhalin
    {675419, 39},    // Europe/Volgograd
    {679096, 7},     // Europe/Copenhagen
    {681504, 14},    // America/Panama
    {683109, 35},    // Pacific/Chuuk
    {688338, 19},    // America/Edmonton
    {692618, 30},    // America/Santiago
    {695903, 46},    // Asia/Irkutsk
    {696273, 7},     // Europe/Brussels
    {697408, 46},    // Asia/Choibalsan
    {702292, 21},    // America/Mazatlan
    {703920, 37},    // Asia/Oral
    {707873, 33},    // Antarctica/Casey
    {711703, 23},    // America/Detroit
    {711788, 41},    // Asia/Almaty
    {712423, 4},     // Africa/Khartoum
    {712988, 7},     // Europe/Vaduz
    {716645, 1},     // Indian/Antananarivo
    {717463, 12},    // America/Argentina/Rio_Gallegos
    {730803, 43},    // Pacific/Tarawa
    {732255, 37},    // Asia/Qyzylorda
    {736937, 41},    // Asia/Thimphu
    {737384, 7},     // Europe/Vatican
    {745820, 19},    // America/Denver
    {746366, 66},    // Asia/Tokyo
    {746788, 48},    // Pacific/Palau
    {749066, 5},     // Libya
    {751039, 37},    // Asia/Ashgabat
    {765182, 41},    // Indian/Chagos
    {770053, 48},    // Asia/Chita
    {778195, 0},     // Atlantic/St_Helena
    {779608, 14},    // America/Cancun
    {790313, 74},    // Australia/West
    {790984, 44},    // Europe/Ulyanovsk
    {794645, 52},    // Europe/Riga
    {796867, 48},    // Asia/Khandyga
    {819359, 20},    // US/Indiana-Starke
    {820461, 17},    // America/Porto_Velho
    {820968, 92},    // HST
    {825000, 5},     // Europe/Kaliningrad
    {825021, 71},    // Australia/North
    {827271, 67},    // Portugal
    {827378, 33},    // Pacific/Ponape
    {828715, 20},    // US/Central
    {833473, 23},    // America/Indiana/Winamac
    {845698, 35},    // Pacific/Port_Moresby
    {845934, 32},    // America/St_Johns
    {853628, 5},     // Africa/Cairo
    {853804, 19},    // America/Inuvik
    {857780, 23},    // America/Indiana/Marengo
    {861210, 20},    // America/Indiana/Tell_City
    {874038, 33},    // Pacific/Efate
    {876028, 0},     // Atlantic/Reykjavik
    {876382, 87},    // Etc/UCT
    {877492, 25},    // America/Thule
    {881218, 95},    // Pacific/Apia
    {883583, 12},    // America/Argentina/Catamarca
    {884797, 43},    // Pacific/Wake
    {889468, 79},    // Etc/GMT+12
    {890274, 77},    // Etc/GMT+10
    {890677, 78},    // Etc/GMT+11
    {891082, 0},     // Etc/GMT
    {893463, 67},    // Atlantic/Madeira
    {904171, 16},    // Canada/Saskatchewan
    {911377, 52},    // EET
    {913956, 0},     // GMT+0
    {920547, 10},    // US/Alaska
    {934775, 7},     // Europe/Bratislava
    {938682, 12},    // America/Argentina/San_Luis
    {939645, 87},    // UTC
    {943222, 44},    // Asia/Tbilisi
    {949217, 18},    // Brazil/Acre
    {951063, 17},    // America/Guyana
    {965414, 85},    // Pacific/Kiritimati
    {973205, 23},    // America/Kentucky/Monticello
    {975038, 23},    // America/Port-au-Prince
    {980806, 18},    // America/Bogota
    {981204, 90},    // Europe/Simferopol
    {987190, 44},    // Europe/Saratov
    {999887, 7},     // Europe/Sarajevo
    {1003911, 58},   // Asia/Kabul
    {1005304, 63},   // Asia/Seoul
    {1007821, 44},   // Asia/Dubai
    {1014376, 10},   // America/Sitka
    {1020394, 22},   // US/Arizona
    {1021091, 67},   // Atlantic/Faroe
    {1027658, 52},   // Europe/Mariehamn
    {1028352, 16},   // America/Regina
    {1029516, 6},    // Africa/El_Aaiun
    {1039495, 36},   // Australia/Victoria
    {1043921, 34},   // Asia/Barnaul
    {1046837, 33},   // Asia/Magadan
    {1051310, 18},   // America/Rio_Branco
    {1052350, 70},   // Australia/Brisbane
    {1056374, 12},   // America/Argentina/Cordoba
    {1061054, 52},   // Europe/Tallinn
    {1064998, 37},   // Asia/Dushanbe
    {1075218, 89},   // Europe/Tiraspol
    {1084768, 3},    // Africa/Ndjamena
    {1086799, 69},   // Australia/Yancowinna
    {1089024, 12},   // Etc/GMT+3
    {1089427, 29},   // Etc/GMT+2
    {1089830, 68},   // Etc/GMT+1
    {1090233, 0},    // Etc/GMT+0
    {1090636, 81},   // Etc/GMT+7
    {1091039, 80},   // Etc/GMT+6
    {1091442, 18},   // Etc/GMT+5
    {1091845, 17},   // Etc/GMT+4
    {1093054, 83},   // Etc/GMT+9
    {1093457, 82},   // Etc/GMT+8
    {1094261, 0},    // Africa/Sao_Tome
    {1097463, 69},   // Australia/Adelaide
    {1104071, 14},   // EST
    {1110416, 29},   // Atlantic/South_Georgia
    {1112624, 44},   // Asia/Yerevan
    {1112853, 9},    // US/Aleutian
    {1118334, 42},   // Asia/Amman
    {1129135, 4},    // Africa/Windhoek
    {1130380, 7},    // Europe/Stockholm
    {1132822, 52},   // Europe/Vilnius
    {1138217, 7},    // Europe/Malta
    {1138914, 55},   // Asia/Jakarta
    {1153929, 0},    // Africa/Bissau
    {1162475, 52},   // Asia/Famagusta
    {1166382, 41},   // Asia/Qostanay
    {1171420, 0},    // America/Danmarkshavn
    {1175981, 12},   // America/Belem
    {1180279, 17},   // America/Cuiaba
    {1181500, 23},   // US/Michigan
    {1185412, 15},   // America/Merida
    {1186379, 7},    // Europe/Zurich
    {1196657, 36},   // Australia/Melbourne
    {1198798, 12},   // America/Maceio
    {1200829, 71},   // Australia/Darwin
    {1203125, 52},   // Europe/Helsinki
    {1204987, 11},   // America/Martinique
    {1208470, 54},   // Asia/Hong_Kong
    {1210986, 72},   // Australia/Eucla
    {1213061, 12},   // Antarctica/Rothera
    {1215578, 17},   // America/Caracas
    {1217188, 7},    // Europe/Luxembourg
    {1217254, 25},   // Atlantic/Bermuda
    {1220402, 7},    // Europe/Belgrade
    {1226645, 12},   // Brazil/East
    {1228331, 27},   // America/Havana
    {1229346, 20},   // America/North_Dakota/Center
    {1229493, 96},   // Pacific/Fiji
    {1233986, 22},   // America/Whitehorse
    {1238271, 92},   // Pacific/Honolulu
    {1247746, 19},   // Navajo
    {1248438, 23},   // America/Indiana/Petersburg
    {1252962, 63},   // Asia/Pyongyang
    {1254429, 0},    // GMT0
    {1256032, 52},   // Europe/Uzhgorod
    {1258171, 7},    // Europe/Ljubljana
    {1263939, 7},    // Europe/San_Marino
    {1270740, 20},   // America/Chicago
    {1283805, 20},   // America/Menominee
    {1296096, 25},   // America/Goose_Bay
    {1296855, 2},    // Africa/Algiers
    {1303250, 37},   // Asia/Aqtobe
    {1304112, 23},   // America/Indiana/Vincennes
    {1308115, 7},    // Europe/Warsaw
    {1319533, 18},   // America/Eirunepe
    {1337063, 87},   // UCT
    {1337990, 83},   // Pacific/Gambier
    {1338312, 1},    // Indian/Mayotte
    {1342523, 23},   // America/Grand_Turk
    {1351228, 7},    // Europe/Gibraltar
    {1361407, 34},   // Asia/Novokuznetsk
    {1365325, 7},    // Europe/Andorra
    {1368647, 19},   // Canada/Mountain
    {1369907, 7},    // Europe/Vienna
    {1371911, 69},   // Australia/Broken_Hill
    {1372423, 67},   // WET
    {1377039, 12},   // America/Santarem
    {1377848, 30},   // Chile/Continental
    {1379920, 68},   // Atlantic/Cape_Verde
    {1395088, 23},   // US/East-Indiana
    {1405309, 12},   // America/Araguaina
    {1408594, 8},    // Africa/Johannesburg
    {1411942, 100},  // Pacific/Norfolk
    {1417058, 15},   // America/Bahia_Banderas
    {1417591, 22},   // America/Fort_Nelson
    {1419250, 36},   // Australia/Sydney
    {1419657, 23},   // America/Toronto
    {1425345, 99},   // US/Samoa
    {1429596, 7},    // Europe/Prague
    {1429753, 82},   // Pacific/Pitcairn
    {1433103, 94},   // Pacific/Chatham
    {1436375, 20},   // America/Matamoros
    {1446971, 35},   // Pacific/Truk
    {1448104, 31},   // America/Scoresbysund
    {1453728, 4},    // Africa/Maputo
    {1454891, 16},   // America/Belize
    {1456669, 56},   // Asia/Jayapura
    {1458268, 23},   // America/New_York
    {1459791, 34},   // Asia/Hovd
    {1462267, 75},   // Pacific/Easter
    {1475507, 0},    // Africa/Monrovia
    {1485189, 20},   // America/Winnipeg
    {1495084, 28},   // America/Miquelon
    {1504159, 22},   // MST
    {1505373, 34},   // Indian/Christmas
    {1510870, 92},   // Pacific/Johnston
    {1520618, 39},   // Europe/Minsk
    {1525516, 37},   // Asia/Atyrau
    {1532373, 11},   // America/Puerto_Rico
    {1535924, 7},    // Africa/Ceuta
    {1540528, 12},   // America/Montevideo
    {1540992, 25},   // Canada/Atlantic
    {1544434, 13},   // America/Asuncion
    {1551375, 7},    // Europe/Oslo
    {1556415, 22},   // America/Phoenix
    {1557856, 4},    // Africa/Juba
    {1559064, 31},   // Atlantic/Azores
    {1568759, 24},   // Canada/Pacific
    {1570889, 9},    // America/Adak
    {1578023, 41},   // Asia/Dhaka
    {1582037, 24},   // US/Pacific
    {1598908, 80},   // Pacific/Galapagos
    {1600593, 50},   // Asia/Colombo
    {1604820, 24},   // America/Los_Angeles
    {1606619, 67},   // Europe/Lisbon
    {1607248, 33},   // Pacific/Pohnpei
    {1609737, 18},   // America/Lima
    {1610087, 17},   // America/La_Paz
    {1610980, 75},   // Chile/EasterIsland
    {1614075, 7},    // Europe/Monaco
    {1617001, 25},   // America/Glace_Bay
    {1617113, 34},   // Asia/Ho_Chi_Minh
    {1618683, 87},   // Etc/Universal
    {1628048, 36},   // Australia/Canberra
    {1628566, 84},   // Pacific/Tongatapu
    {1628682, 22},   // Canada/Yukon
    {1632914, 36},   // Antarctica/Macquarie
    {1651585, 49},   // Asia/Shanghai
    {1651622, 7},    // Europe/Budapest
    {1654743, 39},   // Europe/Kirov
    {1658833, 43},   // Asia/Anadyr
    {1672213, 74},   // Australia/Perth
    {1674610, 16},   // America/Tegucigalpa
    {1675410, 84},   // Pacific/Fakaofo
    {1676415, 17},   // Brazil/West
    {1685232, 34},   // Asia/Krasnoyarsk
    {1692325, 11},   // America/Santo_Domingo
    {1694433, 12},   // America/Argentina/Jujuy
    {1701688, 12},   // America/Recife
    {1716755, 23},   // America/Kentucky/Louisville
    {1719069, 43},   // Pacific/Majuro
    {1721181, 44},   // Europe/Astrakhan
    {1722853, 20},   // America/Indiana/Knox
    {1726451, 5},    // Africa/Tripoli
    {1733838, 23},   // America/Indiana/Vevay
    {1734054, 20},   // America/North_Dakota/Beulah
    {1736591, 7},    // Europe/Paris
    {1738905, 19},   // America/Ojinaga
    {1741247, 0},    // GMT
    {1742182, 16},   // America/Managua
    {1746506, 0},    // Etc/Greenwich
    {1749527, 69},   // Australia/South
    {1749682, 46},   // Asia/Singapore
    {1756562, 20},   // America/North_Dakota/New_Salem
    {1769420, 70},   // Australia/Queensland
    {1774546, 23},   // America/Indiana/Indianapolis
    {1776375, 52},   // Europe/Zaporozhye
    {1778258, 39},   // Europe/Istanbul
    {1781122, 44},   // Indian/Mauritius
    {1784482, 44},   // Indian/Mahe
    {1786941, 39},   // Asia/Qatar
    {1788281, 7},    // Europe/Busingen
    {1798337, 46},   // Asia/Kuching
    {1798422, 76},   // Europe/Dublin
    {1802468, 24},   // America/Tijuana
    {1803694, 14},   // America/Jamaica
    {1832626, 12},   // America/Paramaribo
    {1847810, 37},   // Asia/Tashkent
    {1853077, 43},   // Pacific/Wallis
    {1857751, 32},   // Canada/Newfoundland
    {1861199, 34},   // Asia/Novosibirsk
    {1862715, 39},   // Asia/Riyadh
    {1871852, 87},   // Etc/UTC
    {1884018, 52},   // Europe/Sofia
    {1884747, 17},   // America/Manaus
    {1894052, 7},    // Europe/Skopje
    {1894752, 35},   // Asia/Vladivostok
    {1901000, 87},   // Etc/Zulu
    {1904373, 10},   // America/Nome
    {1905218, 23},   // US/Eastern
    {1921672, 16},   // America/El_Salvador
    {1942188, 10},   // America/Metlakatla
    {1946378, 12},   // America/Fortaleza
    {1951927, 39},   // Asia/Baghdad
    {1959517, 33},   // Asia/Srednekolymsk
    {1961627, 25},   // America/Halifax
    {1962619, 87},   // Zulu
    {1967285, 37},   // Indian/Kerguelen
    {1968744, 38},   // Pacific/Auckland
    {1968987, 7},    // Europe/Zagreb
    {1981568, 22},   // America/Dawson
    {2000737, 34},   // Antarctica/Davis
    {2006408, 99},   // Pacific/Pago_Pago
    {2009827, 43},   // Pacific/Funafuti
    {2013536, 17},   // America/Boa_Vista
    {2015747, 51},   // Asia/Damascus
    {2016109, 26},   // America/Nuuk
    {2017028, 33},   // Pacific/Kosrae
    {2019445, 12},   // America/Argentina/La_Rioja
    {2019448, 36},   // Australia/Hobart
    {2026282, 3},    // Africa/Lagos
    {2028182, 52},   // Asia/Nicosia
    {2033048, 64},   // Asia/Yangon
    {2062776, 7},    // Europe/Amsterdam
    {2068042, 37},   // Indian/Maldives
    {2070769, 12},   // America/Argentina/Salta
    {2071245, 73},   // Australia/LHI
    {2071733, 12},   // America/Bahia
    {2072709, 47},   // Asia/Kolkata
    {2074130, 65},   // Asia/Tehran
    {2075568, 37},   // Etc/GMT-5
    {2075971, 44},   // Etc/GMT-4
    {2076374, 34},   // Etc/GMT-7
    {2076777, 41},   // Etc/GMT-6
    {2077180, 6},    // Etc/GMT-1
    {2077265, 53},   // Asia/Gaza
    {2077583, 0},    // Etc/GMT-0
    {2077986, 39},   // Etc/GMT-3
    {2078389, 86},   // Etc/GMT-2
    {2080404, 48},   // Etc/GMT-9
    {2080807, 46},   // Etc/GMT-8
    {2087116, 64},   // Indian/Cocos
    {2088323, 37},   // Antarctica/Mawson
    {2089285, 44},   // Asia/Baku
    {2090334, 57},   // Asia/Jerusalem
};
#endif

const char *getPosixTZforOlson(const char *olson);

#endif