/*
  EGTimeZoneTable.h - Generated from the IANA tz database.

  Copyright (C) 2026 @steadramon

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
#ifndef EGTIMEZONE_TABLE_H
#define EGTIMEZONE_TABLE_H

// IANA tzdb 2026c. Rerun tools/gen_timezones.py; never edit.
// Included only by EGTimeZone.cpp.

// NUL separated; zones[].offset indexes into this.
static const char TZ_RULES[] PROGMEM =
  "<+00>0\0"
  "<+00>0<+02>-2,M3.5.0/1,M10.5.0/3\0"
  "<+01>-1\0"
  "<+02>-2\0"
  "<+0330>-3:30\0"
  "<+03>-3\0"
  "<+0430>-4:30\0"
  "<+04>-4\0"
  "<+0530>-5:30\0"
  "<+0545>-5:45\0"
  "<+05>-5\0"
  "<+0630>-6:30\0"
  "<+06>-6\0"
  "<+07>-7\0"
  "<+0845>-8:45\0"
  "<+08>-8\0"
  "<+09>-9\0"
  "<+1030>-10:30<+11>-11,M10.1.0,M4.1.0\0"
  "<+10>-10\0"
  "<+11>-11\0"
  "<+11>-11<+12>,M10.1.0,M4.1.0/3\0"
  "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45\0"
  "<+12>-12\0"
  "<+13>-13\0"
  "<+14>-14\0"
  "<-01>1\0"
  "<-01>1<+00>,M3.5.0/0,M10.5.0/1\0"
  "<-02>2\0"
  "<-02>2<-01>,M3.5.0/-1,M10.5.0/0\0"
  "<-03>3\0"
  "<-03>3<-02>,M3.2.0,M11.1.0\0"
  "<-04>4\0"
  "<-04>4<-03>,M9.1.6/24,M4.1.6/24\0"
  "<-05>5\0"
  "<-06>6\0"
  "<-06>6<-05>,M9.1.6/22,M4.1.6/22\0"
  "<-07>7\0"
  "<-08>8\0"
  "<-0930>9:30\0"
  "<-09>9\0"
  "<-10>10\0"
  "<-11>11\0"
  "<-12>12\0"
  "ACST-9:30\0"
  "ACST-9:30ACDT,M10.1.0,M4.1.0/3\0"
  "AEST-10\0"
  "AEST-10AEDT,M10.1.0,M4.1.0/3\0"
  "AKST9AKDT,M3.2.0,M11.1.0\0"
  "AST4\0"
  "AST4ADT,M3.2.0,M11.1.0\0"
  "AWST-8\0"
  "CAT-2\0"
  "CET-1\0"
  "CET-1CEST,M3.5.0,M10.5.0/3\0"
  "CST-8\0"
  "CST5CDT,M3.2.0/0,M11.1.0/1\0"
  "CST6\0"
  "CST6CDT,M3.2.0,M11.1.0\0"
  "ChST-10\0"
  "EAT-3\0"
  "EET-2\0"
  "EET-2EEST,M3.4.4/50,M10.4.4/50\0"
  "EET-2EEST,M3.5.0/0,M10.5.0/0\0"
  "EET-2EEST,M3.5.0/3,M10.5.0/4\0"
  "EET-2EEST,M4.5.5/0,M10.5.4/24\0"
  "EST5\0"
  "EST5EDT,M3.2.0,M11.1.0\0"
  "GMT0\0"
  "GMT0BST,M3.5.0/1,M10.5.0\0"
  "HKT-8\0"
  "HST10\0"
  "HST10HDT,M3.2.0,M11.1.0\0"
  "IST-1GMT0,M10.5.0,M3.5.0/1\0"
  "IST-2IDT,M3.4.4/26,M10.5.0\0"
  "IST-5:30\0"
  "JST-9\0"
  "KST-9\0"
  "MSK-3\0"
  "MST7\0"
  "MST7MDT,M3.2.0,M11.1.0\0"
  "NST3:30NDT,M3.2.0,M11.1.0\0"
  "NZST-12NZDT,M9.5.0,M4.1.0/3\0"
  "PKT-5\0"
  "PST-8\0"
  "PST8PDT,M3.2.0,M11.1.0\0"
  "SAST-2\0"
  "SST11\0"
  "UTC0\0"
  "WAT-1\0"
  "WET0WEST,M3.5.0/1,M10.5.0\0"
  "WIB-7\0"
  "WIT-9\0"
  "WITA-8\0"
  ;

// Sorted by hash for binary search.
PROGMEM static const struct TZoneH {
  uint32_t hash   : 21;
  uint32_t offset : 11;
} zones[] = {
    {3177, 425},     // America/Argentina/Buenos_Aires
    {5424, 577},     // Pacific/Rarotonga
    {6596, 386},     // Brazil/DeNoronha
    {8404, 1277},    // Pacific/Midway
    {16206, 1011},   // Europe/London
    {23764, 751},    // Europe/Berlin
    {25742, 642},    // Australia/Lindeman
    {28550, 919},    // Europe/Nicosia
    {33699, 145},    // Asia/Urumqi
    {34704, 1011},   // Europe/Jersey
    {39895, 839},    // Pacific/Saipan
    {43289, 1158},   // US/Mountain
    {43535, 321},    // Pacific/Kwajalein
    {45558, 459},    // America/Campo_Grande
    {46140, 425},    // America/Cordoba
    {47414, 1036},   // Hongkong
    {47637, 1048},   // America/Atka
    {56581, 811},    // America/Guatemala
    {57398, 585},    // Pacific/Niue
    {61482, 859},    // Asia/Hebron
    {63824, 1153},   // America/Hermosillo
    {75452, 983},    // America/Fort_Wayne
    {75610, 704},    // America/Curacao
    {77251, 704},    // America/Marigot
    {77667, 227},    // Asia/Ust-Nera
    {80762, 498},    // America/Guayaquil
    {83533, 153},    // Asia/Saigon
    {91050, 704},    // America/Blanc-Sablon
    {92809, 425},    // America/Sao_Paulo
    {93028, 983},    // Canada/Eastern
    {97827, 919},    // Europe/Bucharest
    {98146, 227},    // Antarctica/DumontDUrville
    {98189, 1247},   // Mexico/BajaNorte
    {100383, 890},   // Asia/Beirut
    {102841, 978},   // America/Coral_Harbour
    {104536, 182},   // Asia/Dili
    {109229, 321},   // Asia/Kamchatka
    {110344, 948},   // Egypt
    {121092, 425},   // Atlantic/Stanley
    {124286, 1006},  // Africa/Freetown
    {136540, 1147},  // Europe/Moscow
    {138335, 704},   // America/Montserrat
    {153557, 1320},  // Asia/Pontianak
    {156843, 919},   // Europe/Kiev
    {160344, 124},   // Asia/Yekaterinburg
    {161461, 56},    // Iran
    {165199, 816},   // America/Resolute
    {166924, 236},   // Pacific/Guadalcanal
    {172026, 1288},  // Africa/Malabo
    {186333, 751},   // Europe/Rome
    {190474, 1283},  // Universal
    {191305, 751},   // MET
    {193306, 751},   // Atlantic/Jan_Mayen
    {195711, 839},   // Pacific/Guam
    {196047, 1042},  // US/Hawaii
    {200499, 778},   // Asia/Macao
    {201322, 1006},  // Africa/Ouagadougou
    {201945, 919},   // Europe/Athens
    {208700, 182},   // Asia/Yakutsk
    {210977, 778},   // Asia/Macau
    {221293, 174},   // Asia/Brunei
    {222990, 1207},  // Antarctica/South_Pole
    {226052, 811},   // America/Costa_Rica
    {232294, 236},   // Pacific/Bougainville
    {241713, 7},     // Antarctica/Troll
    {245936, 751},   // Europe/Podgorica
    {250998, 650},   // Australia/NSW
    {251033, 983},   // EST5EDT
    {262983, 847},   // Africa/Addis_Ababa
    {267738, 153},   // Asia/Tomsk
    {270075, 983},   // America/Iqaluit
    {274113, 386},   // America/Noronha
    {276475, 751},   // Europe/Tirane
    {277285, 330},   // Pacific/Enderbury
    {279399, 1332},  // Asia/Makassar
    {281938, 1158},  // America/Boise
    {285314, 1011},  // Europe/Guernsey
    {288178, 276},   // NZ-CHAT
    {288555, 190},   // Australia/Lord_Howe
    {289636, 709},   // America/Moncton
    {290785, 425},   // America/Punta_Arenas
    {296680, 1241},  // Asia/Manila
    {300125, 816},   // Canada/Central
    {303007, 1158},  // America/Shiprock
    {303455, 145},   // Asia/Bishkek
    {307480, 124},   // Asia/Aqtau
    {309993, 90},    // Asia/Muscat
    {310155, 425},   // America/Argentina/San_Juan
    {318936, 339},   // Etc/GMT-14
    {320548, 227},   // Etc/GMT-10
    {320951, 236},   // Etc/GMT-11
    {321354, 321},   // Etc/GMT-12
    {321757, 330},   // Etc/GMT-13
    {322216, 650},   // Australia/Currie
    {325064, 978},   // America/Atikokan
    {329953, 811},   // Mexico/General
    {335595, 1153},  // Mexico/BajaSur
    {337525, 983},   // America/Thunder_Bay
    {344612, 816},   // America/Rainy_River
    {346512, 778},   // Asia/Taipei
    {349954, 1006},  // Africa/Nouakchott
    {350456, 847},   // Africa/Dar_es_Salaam
    {362265, 1147},  // W-SU
    {367282, 425},   // America/Argentina/Ushuaia
    {375886, 425},   // America/Argentina/Mendoza
    {379069, 153},   // Asia/Bangkok
    {384126, 983},   // America/Montreal
    {385136, 1288},  // Africa/Kinshasa
    {392475, 425},   // America/Argentina/Tucuman
    {395160, 1294},  // Atlantic/Faeroe
    {398176, 650},   // Australia/Tasmania
    {402710, 321},   // Pacific/Nauru
    {407955, 90},    // Europe/Samara
    {419965, 811},   // America/Swift_Current
    {421329, 1207},  // NZ
    {427478, 679},   // America/Juneau
    {429402, 778},   // Asia/Chongqing
    {431351, 145},   // Asia/Kashgar
    {431659, 1006},  // Africa/Lome
    {431737, 1158},  // America/Cambridge_Bay
    {431765, 751},   // Europe/Madrid
    {445829, 1006},  // Iceland
    {448725, 0},     // Africa/Casablanca
    {456379, 111},   // Asia/Kathmandu
    {457803, 1153},  // America/Vancouver
    {459497, 983},   // America/Nassau
    {461661, 1332},  // Asia/Ujung_Pandang
    {461663, 811},   // America/Monterrey
    {464955, 739},   // Africa/Harare
    {467586, 1294},  // Atlantic/Canary
    {492113, 1011},  // Europe/Belfast
    {492850, 1011},  // Europe/Isle_of_Man
    {498759, 1153},  // America/Dawson_Creek
    {501621, 425},   // America/Cayenne
    {512398, 1006},  // Etc/GMT0
    {513033, 425},   // Antarctica/Palmer
    {513738, 124},   // Asia/Samarkand
    {516616, 1277},  // Pacific/Samoa
    {516962, 778},   // Asia/Chungking
    {519199, 425},   // America/Rosario
    {525358, 145},   // Asia/Omsk
    {526318, 919},   // Europe/Chisinau
    {529695, 1141},  // ROK
    {529783, 811},   // America/Mexico_City
    {532919, 778},   // ROC
    {537346, 811},   // America/Chihuahua
    {540603, 847},   // Africa/Kampala
    {556011, 745},   // Africa/Tunis
    {562127, 704},   // America/Guadeloupe
    {569631, 1006},  // Africa/Dakar
    {569859, 704},   // America/Kralendijk
    {570340, 679},   // America/Anchorage
    {573129, 90},    // Indian/Reunion
    {583325, 227},   // Pacific/Yap
    {586628, 704},   // America/Lower_Princes
    {587591, 679},   // America/Yakutat
    {595614, 816},   // America/Rankin_Inlet
    {601710, 847},   // Indian/Comoro
    {604958, 650},   // Australia/ACT
    {609925, 1099},  // Israel
    {611845, 751},   // Poland
    {614930, 1006},  // GMT-0
    {621209, 704},   // America/Virgin
    {626509, 1235},  // Asia/Karachi
    {627717, 558},   // Pacific/Marquesas
    {639134, 704},   // America/Barbados
    {639738, 236},   // Pacific/Noumea
    {645690, 132},   // Asia/Rangoon
    {648563, 1006},  // Africa/Abidjan
    {653866, 577},   // Pacific/Tahiti
    {657702, 1270},  // Africa/Mbabane
    {657734, 847},   // Africa/Nairobi
    {661560, 174},   // Asia/Ulaanbaatar
    {663785, 236},   // Asia/Sakhalin
    {671411, 425},   // America/Jujuy
    {675419, 1147},  // Europe/Volgograd
    {679096, 751},   // Europe/Copenhagen
    {681504, 978},   // America/Panama
    {683109, 227},   // Pacific/Chuuk
    {685444, 124},   // Antarctica/Vostok
    {685809, 983},   // America/Pangnirtung
    {688338, 811},   // America/Edmonton
    {690567, 704},   // America/Antigua
    {692618, 466},   // America/Santiago
    {692891, 751},   // Arctic/Longyearbyen
    {695903, 174},   // Asia/Irkutsk
    {696273, 751},   // Europe/Brussels
    {697408, 174},   // Asia/Choibalsan
    {702292, 1153},  // America/Mazatlan
    {703920, 124},   // Asia/Oral
    {707873, 174},   // Antarctica/Casey
    {711703, 983},   // America/Detroit
    {711788, 124},   // Asia/Almaty
    {711799, 1207},  // Antarctica/McMurdo
    {712423, 739},   // Africa/Khartoum
    {712988, 751},   // Europe/Vaduz
    {716645, 847},   // Indian/Antananarivo
    {717463, 425},   // America/Argentina/Rio_Gallegos
    {730803, 321},   // Pacific/Tarawa
    {732255, 124},   // Asia/Qyzylorda
    {736937, 145},   // Asia/Thimphu
    {737384, 751},   // Europe/Vatican
    {737794, 69},    // Asia/Istanbul
    {745820, 1158},  // America/Denver
    {746366, 1135},  // Asia/Tokyo
    {746788, 182},   // Pacific/Palau
    {749066, 853},   // Libya
    {751039, 124},   // Asia/Ashgabat
    {765182, 145},   // Indian/Chagos
    {770053, 182},   // Asia/Chita
    {778195, 1006},  // Atlantic/St_Helena
    {779608, 978},   // America/Cancun
    {790313, 732},   // Australia/West
    {790984, 90},    // Europe/Ulyanovsk
    {794645, 919},   // Europe/Riga
    {796867, 182},   // Asia/Khandyga
    {803253, 1006},  // Africa/Bamako
    {809639, 847},   // Africa/Mogadishu
    {819359, 816},   // US/Indiana-Starke
    {820461, 459},   // America/Porto_Velho
    {820802, 1288},  // Africa/Libreville
    {820968, 1042},  // HST
    {825000, 853},   // Europe/Kaliningrad
    {825021, 601},   // Australia/North
    {827251, 425},   // America/Buenos_Aires
    {827271, 1294},  // Portugal
    {827378, 236},   // Pacific/Ponape
    {828715, 816},   // US/Central
    {833473, 983},   // America/Indiana/Winamac
    {836835, 704},   // America/St_Vincent
    {845698, 227},   // Pacific/Port_Moresby
    {845934, 1181},  // America/St_Johns
    {853628, 948},   // Africa/Cairo
    {853804, 1158},  // America/Inuvik
    {857365, 69},    // Asia/Kuwait
    {857780, 983},   // America/Indiana/Marengo
    {861210, 816},   // America/Indiana/Tell_City
    {874038, 236},   // Pacific/Efate
    {876028, 1006},  // Atlantic/Reykjavik
    {876382, 1283},  // Etc/UCT
    {877492, 709},   // America/Thule
    {881218, 330},   // Pacific/Apia
    {883583, 425},   // America/Argentina/Catamarca
    {884797, 321},   // Pacific/Wake
    {889468, 593},   // Etc/GMT+12
    {890274, 577},   // Etc/GMT+10
    {890677, 585},   // Etc/GMT+11
    {891082, 1006},  // Etc/GMT
    {893463, 1294},  // Atlantic/Madeira
    {901474, 1099},  // Asia/Tel_Aviv
    {904171, 811},   // Canada/Saskatchewan
    {906508, 1288},  // Africa/Bangui
    {911377, 919},   // EET
    {913956, 1006},  // GMT+0
    {920547, 679},   // US/Alaska
    {934775, 751},   // Europe/Bratislava
    {938682, 425},   // America/Argentina/San_Luis
    {939645, 1283},  // UTC
    {943222, 90},    // Asia/Tbilisi
    {949217, 498},   // Brazil/Acre
    {951063, 459},   // America/Guyana
    {965414, 339},   // Pacific/Kiritimati
    {971533, 69},    // Asia/Bahrain
    {973205, 983},   // America/Kentucky/Monticello
    {975038, 983},   // America/Port-au-Prince
    {980806, 498},   // America/Bogota
    {981204, 1147},  // Europe/Simferopol
    {981929, 978},   // Jamaica
    {985401, 1247},  // America/Ensenada
    {987190, 90},    // Europe/Saratov
    {992722, 69},    // Asia/Aden
    {994235, 1126},  // Asia/Calcutta
    {997374, 704},   // America/Dominica
    {999887, 751},   // Europe/Sarajevo
    {1000699, 1135}, // Japan
    {1003911, 77},   // Asia/Kabul
    {1005304, 1141}, // Asia/Seoul
    {1007821, 90},   // Asia/Dubai
    {1014376, 679},  // America/Sitka
    {1020394, 1153}, // US/Arizona
    {1021091, 1294}, // Atlantic/Faroe
    {1027658, 919},  // Europe/Mariehamn
    {1028352, 811},  // America/Regina
    {1029516, 0},    // Africa/El_Aaiun
    {1039495, 650},  // Australia/Victoria
    {1043921, 153},  // Asia/Barnaul
    {1046837, 236},  // Asia/Magadan
    {1051310, 498},  // America/Rio_Branco
    {1052350, 642},  // Australia/Brisbane
    {1056374, 425},  // America/Argentina/Cordoba
    {1060994, 704},  // America/St_Lucia
    {1061054, 919},  // Europe/Tallinn
    {1062928, 704},  // America/St_Thomas
    {1063980, 778},  // PRC
    {1064998, 124},  // Asia/Dushanbe
    {1068205, 425},  // America/Catamarca
    {1072445, 739},  // Africa/Gaborone
    {1075218, 919},  // Europe/Tiraspol
    {1084768, 1288}, // Africa/Ndjamena
    {1085336, 1006}, // Africa/Banjul
    {1086799, 611},  // Australia/Yancowinna
    {1089024, 425},  // Etc/GMT+3
    {1089427, 386},  // Etc/GMT+2
    {1089830, 348},  // Etc/GMT+1
    {1090111, 739},  // Africa/Blantyre
    {1090233, 1006}, // Etc/GMT+0
    {1090636, 544},  // Etc/GMT+7
    {1091039, 505},  // Etc/GMT+6
    {1091442, 498},  // Etc/GMT+5
    {1091845, 459},  // Etc/GMT+4
    {1093054, 570},  // Etc/GMT+9
    {1093457, 551},  // Etc/GMT+8
    {1094261, 1006}, // Africa/Sao_Tome
    {1097463, 611},  // Australia/Adelaide
    {1104071, 978},  // EST
    {1109219, 1006}, // Africa/Timbuktu
    {1109996, 1011}, // GB
    {1110416, 386},  // Atlantic/South_Georgia
    {1112624, 90},   // Asia/Yerevan
    {1112853, 1048}, // US/Aleutian
    {1114572, 983},  // America/Louisville
    {1115282, 739},  // Africa/Lubumbashi
    {1118334, 69},   // Asia/Amman
    {1129135, 739},  // Africa/Windhoek
    {1130380, 751},  // Europe/Stockholm
    {1132822, 919},  // Europe/Vilnius
    {1138217, 751},  // Europe/Malta
    {1138914, 1320}, // Asia/Jakarta
    {1147945, 425},  // America/Argentina/ComodRivadavia
    {1153584, 1288}, // Africa/Douala
    {1153793, 124},  // Asia/Ashkhabad
    {1153929, 1006}, // Africa/Bissau
    {1162475, 919},  // Asia/Famagusta
    {1164709, 393},  // America/Godthab
    {1166382, 124},  // Asia/Qostanay
    {1171420, 1006}, // America/Danmarkshavn
    {1173424, 1247}, // PST8PDT
    {1174312, 1158}, // America/Ciudad_Juarez
    {1174363, 1006}, // Greenwich
    {1175981, 425},  // America/Belem
    {1180279, 459},  // America/Cuiaba
    {1181500, 983},  // US/Michigan
    {1185412, 811},  // America/Merida
    {1186379, 751},  // Europe/Zurich
    {1194669, 704},  // America/Anguilla
    {1196657, 650},  // Australia/Melbourne
    {1198798, 425},  // America/Maceio
    {1199087, 69},   // Turkey
    {1200829, 601},  // Australia/Darwin
    {1203125, 919},  // Europe/Helsinki
    {1204987, 704},  // America/Martinique
    {1208470, 1036}, // Asia/Hong_Kong
    {1209491, 145},  // Asia/Thimbu
    {1210986, 161},  // Australia/Eucla
    {1213061, 425},  // Antarctica/Rothera
    {1215578, 459},  // America/Caracas
    {1217188, 751},  // Europe/Luxembourg
    {1217254, 709},  // Atlantic/Bermuda
    {1220402, 751},  // Europe/Belgrade
    {1221000, 1247}, // America/Santa_Isabel
    {1226645, 425},  // Brazil/East
    {1228331, 784},  // America/Havana
    {1229346, 816},  // America/North_Dakota/Center
    {1229493, 321},  // Pacific/Fiji
    {1233986, 1153}, // America/Whitehorse
    {1235835, 69},   // Antarctica/Syowa
    {1236962, 983},  // America/Nipigon
    {1238271, 1042}, // Pacific/Honolulu
    {1241695, 847},  // Africa/Asmera
    {1241995, 739},  // Africa/Kigali
    {1247746, 1158}, // Navajo
    {1248438, 983},  // America/Indiana/Petersburg
    {1252962, 1141}, // Asia/Pyongyang
    {1254429, 1006}, // GMT0
    {1255107, 739},  // Africa/Bujumbura
    {1256032, 919},  // Europe/Uzhgorod
    {1258171, 751},  // Europe/Ljubljana
    {1263939, 751},  // Europe/San_Marino
    {1270740, 816},  // America/Chicago
    {1271679, 1158}, // MST7MDT
    {1279015, 1288}, // Africa/Porto-Novo
    {1283805, 816},  // America/Menominee
    {1292061, 811},  // America/Yellowknife
    {1296096, 709},  // America/Goose_Bay
    {1296855, 745},  // Africa/Algiers
    {1303250, 124},  // Asia/Aqtobe
    {1304112, 983},  // America/Indiana/Vincennes
    {1306975, 321},  // Kwajalein
    {1308115, 751},  // Europe/Warsaw
    {1316301, 1288}, // Africa/Niamey
    {1319533, 498},  // America/Eirunepe
    {1337063, 1283}, // UCT
    {1337990, 570},  // Pacific/Gambier
    {1338312, 847},  // Indian/Mayotte
    {1342523, 983},  // America/Grand_Turk
    {1344460, 425},  // America/Mendoza
    {1351228, 751},  // Europe/Gibraltar
    {1361407, 153},  // Asia/Novokuznetsk
    {1365325, 751},  // Europe/Andorra
    {1368647, 811},  // Canada/Mountain
    {1369907, 751},  // Europe/Vienna
    {1371911, 611},  // Australia/Broken_Hill
    {1372423, 1294}, // WET
    {1377039, 425},  // America/Santarem
    {1377848, 466},  // Chile/Continental
    {1379920, 348},  // Atlantic/Cape_Verde
    {1395088, 983},  // US/East-Indiana
    {1405309, 425},  // America/Araguaina
    {1408594, 1270}, // Africa/Johannesburg
    {1411942, 245},  // Pacific/Norfolk
    {1416215, 1270}, // Africa/Maseru
    {1417058, 811},  // America/Bahia_Banderas
    {1417591, 1153}, // America/Fort_Nelson
    {1419250, 650},  // Australia/Sydney
    {1419657, 983},  // America/Toronto
    {1425345, 1277}, // US/Samoa
    {1429596, 751},  // Europe/Prague
    {1429753, 551},  // Pacific/Pitcairn
    {1433103, 276},  // Pacific/Chatham
    {1436375, 816},  // America/Matamoros
    {1438897, 1006}, // Africa/Conakry
    {1446971, 227},  // Pacific/Truk
    {1448104, 393},  // America/Scoresbysund
    {1453728, 739},  // Africa/Maputo
    {1454891, 811},  // America/Belize
    {1456669, 1326}, // Asia/Jayapura
    {1458268, 983},  // America/New_York
    {1459791, 153},  // Asia/Hovd
    {1462267, 512},  // Pacific/Easter
    {1475245, 704},  // America/St_Kitts
    {1475507, 1006}, // Africa/Monrovia
    {1485189, 816},  // America/Winnipeg
    {1495084, 432},  // America/Miquelon
    {1504159, 1153}, // MST
    {1505373, 153},  // Indian/Christmas
    {1510870, 1042}, // Pacific/Johnston
    {1520618, 69},   // Europe/Minsk
    {1525516, 124},  // Asia/Atyrau
    {1530094, 704},  // America/Grenada
    {1532373, 704},  // America/Puerto_Rico
    {1535924, 751},  // Africa/Ceuta
    {1540528, 425},  // America/Montevideo
    {1540992, 709},  // Canada/Atlantic
    {1544434, 425},  // America/Asuncion
    {1551375, 751},  // Europe/Oslo
    {1556415, 1153}, // America/Phoenix
    {1557856, 739},  // Africa/Juba
    {1559064, 355},  // Atlantic/Azores
    {1568759, 1153}, // Canada/Pacific
    {1570889, 1048}, // America/Adak
    {1571112, 498},  // America/Porto_Acre
    {1578023, 145},  // Asia/Dhaka
    {1582037, 1247}, // US/Pacific
    {1598908, 505},  // Pacific/Galapagos
    {1600593, 98},   // Asia/Colombo
    {1601819, 111},  // Asia/Katmandu
    {1603047, 704},  // America/Port_of_Spain
    {1604820, 1247}, // America/Los_Angeles
    {1606619, 1294}, // Europe/Lisbon
    {1607248, 236},  // Pacific/Pohnpei
    {1607370, 1153}, // America/Creston
    {1608844, 784},  // Cuba
    {1609737, 498},  // America/Lima
    {1610087, 459},  // America/La_Paz
    {1610980, 512},  // Chile/EasterIsland
    {1612262, 1288}, // Africa/Brazzaville
    {1614075, 751},  // Europe/Monaco
    {1617001, 709},  // America/Glace_Bay
    {1617113, 153},  // Asia/Ho_Chi_Minh
    {1618683, 1283}, // Etc/Universal
    {1628048, 650},  // Australia/Canberra
    {1628566, 330},  // Pacific/Tongatapu
    {1628682, 1153}, // Canada/Yukon
    {1632083, 751},  // CET
    {1632914, 650},  // Antarctica/Macquarie
    {1641046, 425},  // America/Coyhaique
    {1651585, 778},  // Asia/Shanghai
    {1651622, 751},  // Europe/Budapest
    {1654743, 1147}, // Europe/Kirov
    {1658833, 321},  // Asia/Anadyr
    {1660911, 174},  // Asia/Ulan_Bator
    {1664872, 816},  // America/Knox_IN
    {1672213, 732},  // Australia/Perth
    {1674610, 811},  // America/Tegucigalpa
    {1675410, 330},  // Pacific/Fakaofo
    {1676415, 459},  // Brazil/West
    {1685232, 153},  // Asia/Krasnoyarsk
    {1692325, 704},  // America/Santo_Domingo
    {1694433, 425},  // America/Argentina/Jujuy
    {1698695, 1288}, // Africa/Luanda
    {1701688, 425},  // America/Recife
    {1708083, 739},  // Africa/Lusaka
    {1710030, 816},  // CST6CDT
    {1713047, 919},  // Europe/Kyiv
    {1713796, 174},  // Asia/Kuala_Lumpur
    {1716755, 983},  // America/Kentucky/Louisville
    {1719069, 321},  // Pacific/Majuro
    {1721181, 90},   // Europe/Astrakhan
    {1722440, 1006}, // Africa/Accra
    {1722853, 816},  // America/Indiana/Knox
    {1726451, 853},  // Africa/Tripoli
    {1733838, 983},  // America/Indiana/Vevay
    {1734054, 816},  // America/North_Dakota/Beulah
    {1736591, 751},  // Europe/Paris
    {1738905, 816},  // America/Ojinaga
    {1741247, 1006}, // GMT
    {1742182, 811},  // America/Managua
    {1746506, 1006}, // Etc/Greenwich
    {1749527, 611},  // Australia/South
    {1749682, 174},  // Asia/Singapore
    {1756562, 816},  // America/North_Dakota/New_Salem
    {1769420, 642},  // Australia/Queensland
    {1774546, 983},  // America/Indiana/Indianapolis
    {1776375, 919},  // Europe/Zaporozhye
    {1778258, 69},   // Europe/Istanbul
    {1781122, 90},   // Indian/Mauritius
    {1784482, 90},   // Indian/Mahe
    {1786941, 69},   // Asia/Qatar
    {1788281, 751},  // Europe/Busingen
    {1798337, 174},  // Asia/Kuching
    {1798422, 1072}, // Europe/Dublin
    {1802468, 1247}, // America/Tijuana
    {1803694, 978},  // America/Jamaica
    {1832626, 425},  // America/Paramaribo
    {1841914, 1072}, // Eire
    {1847810, 124},  // Asia/Tashkent
    {1850225, 983},  // America/Indianapolis
    {1853077, 321},  // Pacific/Wallis
    {1857751, 1181}, // Canada/Newfoundland
    {1861199, 153},  // Asia/Novosibirsk
    {1862715, 69},   // Asia/Riyadh
    {1863529, 153},  // Asia/Vientiane
    {1863787, 847},  // Africa/Asmara
    {1871852, 1283}, // Etc/UTC
    {1884018, 919},  // Europe/Sofia
    {1884747, 459},  // America/Manaus
    {1894052, 751},  // Europe/Skopje
    {1894752, 227},  // Asia/Vladivostok
    {1900518, 330},  // Pacific/Kanton
    {1901000, 1283}, // Etc/Zulu
    {1902986, 153},  // Asia/Phnom_Penh
    {1904373, 679},  // America/Nome
    {1905218, 983},  // US/Eastern
    {1906241, 174},  // Singapore
    {1917776, 778},  // Asia/Harbin
    {1921672, 811},  // America/El_Salvador
    {1938433, 704},  // America/St_Barthelemy
    {1942188, 679},  // America/Metlakatla
    {1946378, 425},  // America/Fortaleza
    {1951927, 69},   // Asia/Baghdad
    {1958859, 704},  // America/Tortola
    {1959517, 236},  // Asia/Srednekolymsk
    {1961627, 709},  // America/Halifax
    {1962619, 1283}, // Zulu
    {1967285, 124},  // Indian/Kerguelen
    {1968744, 1207}, // Pacific/Auckland
    {1968987, 751},  // Europe/Zagreb
    {1970831, 704},  // America/Aruba
    {1981568, 1153}, // America/Dawson
    {1987977, 978},  // America/Cayman
    {2000737, 153},  // Antarctica/Davis
    {2006054, 847},  // Africa/Djibouti
    {2006408, 1277}, // Pacific/Pago_Pago
    {2009827, 321},  // Pacific/Funafuti
    {2013536, 459},  // America/Boa_Vista
    {2015747, 69},   // Asia/Damascus
    {2016109, 393},  // America/Nuuk
    {2017028, 236},  // Pacific/Kosrae
    {2019445, 425},  // America/Argentina/La_Rioja
    {2019448, 650},  // Australia/Hobart
    {2023156, 1011}, // GB-Eire
    {2026282, 1288}, // Africa/Lagos
    {2028182, 919},  // Asia/Nicosia
    {2033048, 132},  // Asia/Yangon
    {2062776, 751},  // Europe/Amsterdam
    {2068042, 124},  // Indian/Maldives
    {2070769, 425},  // America/Argentina/Salta
    {2071245, 190},  // Australia/LHI
    {2071733, 425},  // America/Bahia
    {2072709, 1126}, // Asia/Kolkata
    {2074130, 56},   // Asia/Tehran
    {2075568, 124},  // Etc/GMT-5
    {2075971, 90},   // Etc/GMT-4
    {2076374, 153},  // Etc/GMT-7
    {2076777, 145},  // Etc/GMT-6
    {2077180, 40},   // Etc/GMT-1
    {2077265, 859},  // Asia/Gaza
    {2077583, 1006}, // Etc/GMT-0
    {2077986, 69},   // Etc/GMT-3
    {2078389, 48},   // Etc/GMT-2
    {2080404, 182},  // Etc/GMT-9
    {2080807, 174},  // Etc/GMT-8
    {2087116, 132},  // Indian/Cocos
    {2088323, 124},  // Antarctica/Mawson
    {2089285, 90},   // Asia/Baku
    {2090334, 1099}, // Asia/Jerusalem
    {2094940, 145},  // Asia/Dacca
};

#endif
