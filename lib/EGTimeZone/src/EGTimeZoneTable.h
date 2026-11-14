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

// IANA tzdb 2026e. Rerun tools/gen_timezones.py; never edit.
// Included only by EGTimeZone.cpp.

// NUL separated; zones[] rule n is the nth
static const char TZ_RULES[] PROGMEM =
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

static const uint32_t TZ_SEED = 5;

// 17 bit hash, 7 bit rule, big endian
#define TZ(h, r) { (uint8_t)((h) >> 9), (uint8_t)((h) >> 1), (uint8_t)((h) << 7 | (r)) }

// Sorted by hash for binary search.
PROGMEM static const uint8_t zones[][3] = {
    TZ(382, 14),    // Asia/Brunei
    TZ(568, 14),    // Antarctica/Casey
    TZ(721, 45),    // Australia/NSW
    TZ(1705, 65),   // America/Fort_Wayne
    TZ(2521, 75),   // Asia/Seoul
    TZ(2966, 30),   // America/Guyana
    TZ(3119, 77),   // America/Whitehorse
    TZ(3874, 47),   // America/St_Vincent
    TZ(4017, 17),   // Antarctica/DumontDUrville
    TZ(4047, 39),   // Pacific/Rarotonga
    TZ(4114, 56),   // America/Ojinaga
    TZ(4324, 52),   // Poland
    TZ(4452, 12),   // Asia/Ho_Chi_Minh
    TZ(4464, 26),   // Etc/GMT+2
    TZ(4614, 28),   // America/Buenos_Aires
    TZ(4722, 5),    // Asia/Kabul
    TZ(4726, 43),   // Australia/Adelaide
    TZ(4864, 62),   // Europe/Zaporozhye
    TZ(4867, 28),   // Etc/GMT+3
    TZ(5263, 76),   // Europe/Moscow
    TZ(5270, 66),   // Etc/GMT+0
    TZ(5351, 62),   // Asia/Nicosia
    TZ(5518, 69),   // Pacific/Honolulu
    TZ(5646, 49),   // Australia/Perth
    TZ(5673, 24),   // Etc/GMT+1
    TZ(5674, 65),   // America/Indiana/Winamac
    TZ(6012, 52),   // Europe/Sarajevo
    TZ(6076, 33),   // Etc/GMT+6
    TZ(6382, 58),   // Africa/Addis_Ababa
    TZ(6391, 15),   // Pacific/Palau
    TZ(6479, 35),   // Etc/GMT+7
    TZ(6602, 62),   // Europe/Athens
    TZ(6882, 30),   // Etc/GMT+4
    TZ(7055, 45),   // Antarctica/Macquarie
    TZ(7150, 28),   // America/Belem
    TZ(7176, 52),   // Europe/Oslo
    TZ(7285, 32),   // Etc/GMT+5
    TZ(7338, 64),   // America/Coral_Harbour
    TZ(7393, 55),   // America/Tegucigalpa
    TZ(7680, 30),   // Brazil/West
    TZ(7956, 27),   // America/Nuuk
    TZ(8161, 11),   // Asia/Dacca
    TZ(8310, 45),   // Australia/Melbourne
    TZ(8447, 10),   // Asia/Rangoon
    TZ(8494, 36),   // Etc/GMT+8
    TZ(8897, 38),   // Etc/GMT+9
    TZ(8976, 64),   // EST
    TZ(9639, 66),   // Africa/Ouagadougou
    TZ(9764, 47),   // America/Marigot
    TZ(10097, 13),  // Australia/Eucla
    TZ(10286, 48),  // America/Glace_Bay
    TZ(10782, 55),  // America/Swift_Current
    TZ(10784, 64),  // America/Cayman
    TZ(10809, 45),  // Australia/Canberra
    TZ(10958, 4),   // Asia/Kuwait
    TZ(11009, 50),  // Africa/Lubumbashi
    TZ(11290, 52),  // Europe/Brussels
    TZ(11407, 9),   // Asia/Oral
    TZ(11742, 50),  // Africa/Gaborone
    TZ(11828, 83),  // US/Pacific
    TZ(12188, 73),  // Asia/Kolkata
    TZ(12261, 47),  // America/Curacao
    TZ(12561, 65),  // Canada/Eastern
    TZ(12641, 55),  // America/Inuvik
    TZ(12944, 32),  // America/Eirunepe
    TZ(13765, 11),  // Indian/Chagos
    TZ(13784, 28),  // America/Argentina/Buenos_Aires
    TZ(13946, 37),  // Pacific/Marquesas
    TZ(15150, 47),  // America/St_Barthelemy
    TZ(15247, 24),  // Atlantic/Cape_Verde
    TZ(15358, 21),  // Pacific/Tarawa
    TZ(15451, 30),  // America/Campo_Grande
    TZ(16020, 6),   // Asia/Dubai
    TZ(16148, 52),  // Europe/Andorra
    TZ(16199, 63),  // Egypt
    TZ(16273, 12),  // Asia/Phnom_Penh
    TZ(16567, 66),  // Etc/GMT0
    TZ(16978, 26),  // America/Noronha
    TZ(16981, 78),  // America/Denver
    TZ(17648, 87),  // Africa/Luanda
    TZ(17652, 52),  // Arctic/Longyearbyen
    TZ(17678, 6),   // Asia/Muscat
    TZ(18000, 52),  // Europe/Bratislava
    TZ(18358, 47),  // America/Guadeloupe
    TZ(18496, 21),  // Pacific/Wallis
    TZ(18546, 4),   // Antarctica/Syowa
    TZ(19439, 48),  // Canada/Atlantic
    TZ(19447, 78),  // America/Ciudad_Juarez
    TZ(19768, 48),  // America/Halifax
    TZ(19870, 51),  // Africa/Tunis
    TZ(20016, 9),   // Indian/Kerguelen
    TZ(20131, 18),  // Pacific/Pohnpei
    TZ(20512, 4),   // Asia/Damascus
    TZ(20523, 77),  // Canada/Yukon
    TZ(20572, 12),  // Asia/Novokuznetsk
    TZ(20711, 56),  // America/Rankin_Inlet
    TZ(21003, 18),  // Pacific/Bougainville
    TZ(21675, 44),  // Australia/Lindeman
    TZ(21815, 69),  // Pacific/Johnston
    TZ(22361, 28),  // America/Recife
    TZ(22476, 47),  // America/Tortola
    TZ(22660, 66),  // Africa/Monrovia
    TZ(22763, 46),  // America/Anchorage
    TZ(22831, 22),  // Pacific/Apia
    TZ(23644, 14),  // Asia/Kuching
    TZ(23724, 46),  // America/Nome
    TZ(24071, 46),  // America/Juneau
    TZ(24119, 32),  // America/Bogota
    TZ(24426, 11),  // Asia/Bishkek
    TZ(24612, 52),  // Europe/San_Marino
    TZ(25275, 31),  // Chile/Continental
    TZ(25489, 62),  // Europe/Chisinau
    TZ(25572, 52),  // Europe/Monaco
    TZ(26118, 9),   // Asia/Qyzylorda
    TZ(26256, 11),  // Asia/Thimbu
    TZ(26401, 27),  // America/Scoresbysund
    TZ(27031, 62),  // Europe/Sofia
    TZ(27107, 72),  // Asia/Jerusalem
    TZ(27777, 55),  // America/Regina
    TZ(28153, 65),  // America/Indiana/Vevay
    TZ(28163, 68),  // Hongkong
    TZ(28686, 58),  // Africa/Mogadishu
    TZ(28883, 87),  // Africa/Ndjamena
    TZ(28900, 86),  // Etc/Universal
    TZ(29308, 52),  // Europe/Tirane
    TZ(29379, 62),  // Europe/Tallinn
    TZ(29406, 11),  // Asia/Dhaka
    TZ(29421, 52),  // Africa/Ceuta
    TZ(29529, 80),  // Pacific/Auckland
    TZ(29706, 14),  // Singapore
    TZ(29895, 17),  // Pacific/Port_Moresby
    TZ(30811, 76),  // Europe/Simferopol
    TZ(31020, 65),  // America/Kentucky/Louisville
    TZ(31209, 4),   // Asia/Istanbul
    TZ(32460, 88),  // WET
    TZ(32639, 65),  // America/Indiana/Marengo
    TZ(33051, 18),  // Pacific/Ponape
    TZ(33490, 56),  // America/Menominee
    TZ(33667, 15),  // Asia/Dili
    TZ(33964, 52),  // Europe/Warsaw
    TZ(34080, 47),  // America/Aruba
    TZ(34156, 72),  // Israel
    TZ(34316, 21),  // Pacific/Wake
    TZ(34536, 77),  // MST
    TZ(34586, 78),  // America/Cambridge_Bay
    TZ(34795, 6),   // Asia/Tbilisi
    TZ(35025, 9),   // Asia/Tashkent
    TZ(35086, 8),   // Asia/Kathmandu
    TZ(35457, 63),  // Africa/Cairo
    TZ(35656, 78),  // MST7MDT
    TZ(35844, 62),  // Europe/Kyiv
    TZ(35888, 50),  // Africa/Blantyre
    TZ(36395, 50),  // Africa/Juba
    TZ(36453, 77),  // America/Mazatlan
    TZ(36527, 47),  // America/Dominica
    TZ(37143, 28),  // America/Cordoba
    TZ(37173, 85),  // Pacific/Midway
    TZ(37415, 78),  // Navajo
    TZ(38032, 62),  // Europe/Kiev
    TZ(38051, 62),  // Europe/Vilnius
    TZ(38510, 52),  // Europe/Paris
    TZ(38531, 65),  // America/Indiana/Petersburg
    TZ(38787, 87),  // Africa/Bangui
    TZ(38836, 91),  // Asia/Makassar
    TZ(38908, 77),  // America/Fort_Nelson
    TZ(39160, 77),  // America/Vancouver
    TZ(39305, 84),  // Africa/Johannesburg
    TZ(39320, 9),   // Asia/Ashkhabad
    TZ(39390, 87),  // Africa/Niamey
    TZ(39681, 60),  // Asia/Hebron
    TZ(39706, 77),  // Mexico/BajaSur
    TZ(39882, 28),  // America/Sao_Paulo
    TZ(40086, 57),  // Pacific/Saipan
    TZ(40107, 55),  // America/El_Salvador
    TZ(40111, 83),  // America/Tijuana
    TZ(40252, 15),  // Asia/Khandyga
    TZ(40586, 65),  // America/Toronto
    TZ(41357, 52),  // Europe/Podgorica
    TZ(41788, 32),  // America/Lima
    TZ(41964, 4),   // Asia/Qatar
    TZ(41983, 9),   // Asia/Samarkand
    TZ(42044, 21),  // Pacific/Fiji
    TZ(42437, 19),  // Pacific/Norfolk
    TZ(42454, 51),  // Africa/Algiers
    TZ(42634, 4),   // Asia/Baghdad
    TZ(42824, 47),  // America/St_Kitts
    TZ(43054, 30),  // America/Manaus
    TZ(43343, 87),  // Africa/Douala
    TZ(43431, 83),  // PST8PDT
    TZ(43563, 28),  // America/Mendoza
    TZ(44019, 28),  // America/Asuncion
    TZ(44108, 83),  // America/Ensenada
    TZ(44321, 6),   // Indian/Mahe
    TZ(44744, 16),  // Australia/Lord_Howe
    TZ(44820, 12),  // Asia/Barnaul
    TZ(45190, 76),  // Europe/Volgograd
    TZ(45323, 58),  // Africa/Nairobi
    TZ(45345, 28),  // America/Argentina/Mendoza
    TZ(45552, 56),  // America/Indiana/Knox
    TZ(45975, 52),  // Europe/Berlin
    TZ(45983, 48),  // Atlantic/Bermuda
    TZ(46204, 21),  // Pacific/Kwajalein
    TZ(46307, 65),  // US/Eastern
    TZ(47164, 52),  // Europe/Zagreb
    TZ(47190, 50),  // Africa/Bujumbura
    TZ(47472, 80),  // NZ
    TZ(47487, 22),  // Pacific/Kanton
    TZ(47635, 14),  // Asia/Choibalsan
    TZ(47749, 45),  // Australia/Currie
    TZ(48157, 17),  // Asia/Vladivostok
    TZ(48396, 17),  // Asia/Ust-Nera
    TZ(48679, 14),  // Asia/Singapore
    TZ(48960, 28),  // America/Argentina/La_Rioja
    TZ(49061, 65),  // America/New_York
    TZ(49424, 50),  // Africa/Harare
    TZ(49783, 45),  // Australia/Sydney
    TZ(49999, 65),  // US/Michigan
    TZ(50243, 30),  // America/Boa_Vista
    TZ(50268, 21),  // Asia/Kamchatka
    TZ(50327, 66),  // GMT+0
    TZ(50339, 28),  // America/Maceio
    TZ(50777, 64),  // America/Panama
    TZ(50871, 87),  // Africa/Kinshasa
    TZ(50895, 89),  // Asia/Jakarta
    TZ(51144, 40),  // Etc/GMT+11
    TZ(51187, 82),  // Asia/Manila
    TZ(51547, 39),  // Etc/GMT+10
    TZ(52353, 41),  // Etc/GMT+12
    TZ(53049, 9),   // Asia/Aqtau
    TZ(53235, 4),   // Asia/Amman
    TZ(53294, 17),  // Pacific/Truk
    TZ(53585, 80),  // Antarctica/South_Pole
    TZ(53634, 47),  // America/Kralendijk
    TZ(53844, 47),  // America/Virgin
    TZ(53847, 67),  // GB-Eire
    TZ(53987, 53),  // Asia/Harbin
    TZ(54169, 59),  // Libya
    TZ(54178, 78),  // America/Shiprock
    TZ(54478, 62),  // EET
    TZ(54547, 48),  // America/Moncton
    TZ(54860, 36),  // Pacific/Pitcairn
    TZ(54893, 87),  // Africa/Malabo
    TZ(55078, 47),  // America/Martinique
    TZ(55453, 58),  // Indian/Mayotte
    TZ(55551, 65),  // America/Indiana/Indianapolis
    TZ(55704, 20),  // Pacific/Chatham
    TZ(56477, 65),  // America/Louisville
    TZ(56606, 47),  // America/Santo_Domingo
    TZ(56888, 50),  // Africa/Kigali
    TZ(57227, 44),  // Australia/Brisbane
    TZ(57309, 18),  // Pacific/Kosrae
    TZ(57412, 11),  // Asia/Urumqi
    TZ(57480, 79),  // Canada/Newfoundland
    TZ(57538, 34),  // Pacific/Easter
    TZ(58259, 9),   // Asia/Atyrau
    TZ(58956, 84),  // Africa/Maseru
    TZ(58965, 72),  // Asia/Tel_Aviv
    TZ(59002, 62),  // Europe/Riga
    TZ(59164, 83),  // Mexico/BajaNorte
    TZ(59335, 65),  // America/Montreal
    TZ(59343, 75),  // Asia/Pyongyang
    TZ(59827, 47),  // America/Lower_Princes
    TZ(59941, 47),  // America/Grenada
    TZ(60251, 6),   // Indian/Mauritius
    TZ(60329, 26),  // Atlantic/South_Georgia
    TZ(60368, 28),  // America/Argentina/ComodRivadavia
    TZ(61018, 66),  // Africa/Dakar
    TZ(61268, 18),  // Asia/Magadan
    TZ(61854, 57),  // Pacific/Guam
    TZ(61944, 89),  // Asia/Pontianak
    TZ(61988, 50),  // Africa/Windhoek
    TZ(62073, 46),  // America/Metlakatla
    TZ(62114, 55),  // America/Yellowknife
    TZ(62367, 69),  // HST
    TZ(62415, 47),  // America/Blanc-Sablon
    TZ(62947, 65),  // US/East-Indiana
    TZ(63350, 28),  // America/Argentina/Jujuy
    TZ(63459, 64),  // America/Rainy_River
    TZ(63533, 29),  // America/Miquelon
    TZ(63610, 52),  // Europe/Ljubljana
    TZ(63978, 55),  // America/Belize
    TZ(64357, 34),  // Chile/EasterIsland
    TZ(64446, 66),  // Atlantic/St_Helena
    TZ(64592, 86),  // UCT
    TZ(64747, 6),   // Europe/Saratov
    TZ(65123, 52),  // Europe/Copenhagen
    TZ(65281, 45),  // Australia/ACT
    TZ(65667, 25),  // Atlantic/Azores
    TZ(65889, 56),  // America/Indiana/Tell_City
    TZ(65993, 55),  // America/Managua
    TZ(66331, 23),  // Pacific/Kiritimati
    TZ(66496, 66),  // Etc/GMT-0
    TZ(66728, 9),   // Asia/Ashgabat
    TZ(66899, 1),   // Etc/GMT-1
    TZ(67142, 52),  // MET
    TZ(67163, 48),  // America/Goose_Bay
    TZ(67268, 43),  // Australia/Broken_Hill
    TZ(67302, 2),   // Etc/GMT-2
    TZ(67705, 4),   // Etc/GMT-3
    TZ(67919, 12),  // Asia/Tomsk
    TZ(67992, 28),  // America/Punta_Arenas
    TZ(68108, 6),   // Etc/GMT-4
    TZ(68301, 67),  // Europe/Guernsey
    TZ(68511, 9),   // Etc/GMT-5
    TZ(68655, 83),  // America/Los_Angeles
    TZ(68896, 52),  // CET
    TZ(68914, 11),  // Etc/GMT-6
    TZ(69046, 66),  // Africa/Sao_Tome
    TZ(69143, 40),  // Pacific/Niue
    TZ(69149, 66),  // Atlantic/Reykjavik
    TZ(69308, 28),  // America/Argentina/Tucuman
    TZ(69317, 12),  // Etc/GMT-7
    TZ(69561, 20),  // NZ-CHAT
    TZ(69647, 56),  // America/Knox_IN
    TZ(69720, 14),  // Etc/GMT-8
    TZ(69831, 85),  // Pacific/Samoa
    TZ(69933, 77),  // America/Creston
    TZ(70123, 15),  // Etc/GMT-9
    TZ(70220, 42),  // Australia/Darwin
    TZ(70431, 31),  // America/Santiago
    TZ(71066, 65),  // America/Thunder_Bay
    TZ(71261, 28),  // America/Argentina/Cordoba
    TZ(71636, 49),  // Australia/West
    TZ(71772, 15),  // Asia/Chita
    TZ(72540, 81),  // Asia/Karachi
    TZ(73017, 65),  // America/Nipigon
    TZ(73060, 66),  // GMT
    TZ(73278, 88),  // Atlantic/Faroe
    TZ(73328, 55),  // America/Mexico_City
    TZ(73351, 53),  // Asia/Chongqing
    TZ(73786, 28),  // America/Cayenne
    TZ(74544, 28),  // America/Rosario
    TZ(75348, 12),  // Asia/Bangkok
    TZ(75507, 33),  // Pacific/Galapagos
    TZ(75840, 55),  // Canada/Mountain
    TZ(76050, 27),  // America/Godthab
    TZ(76063, 66),  // Africa/Banjul
    TZ(76323, 53),  // Asia/Taipei
    TZ(76557, 28),  // Atlantic/Stanley
    TZ(76571, 88),  // Atlantic/Faeroe
    TZ(76573, 52),  // Europe/Vatican
    TZ(76604, 66),  // GMT0
    TZ(76694, 6),   // Asia/Baku
    TZ(76981, 71),  // Europe/Dublin
    TZ(77310, 80),  // Antarctica/McMurdo
    TZ(77433, 32),  // America/Guayaquil
    TZ(77837, 55),  // America/Merida
    TZ(78466, 45),  // Australia/Victoria
    TZ(78515, 18),  // Pacific/Noumea
    TZ(78769, 87),  // Africa/Libreville
    TZ(79042, 28),  // Brazil/East
    TZ(79082, 54),  // America/Havana
    TZ(79334, 52),  // Europe/Madrid
    TZ(79410, 32),  // Brazil/Acre
    TZ(79425, 67),  // Europe/London
    TZ(79687, 9),   // Asia/Almaty
    TZ(79791, 39),  // Pacific/Tahiti
    TZ(79877, 64),  // America/Jamaica
    TZ(80256, 9),   // Antarctica/Mawson
    TZ(80309, 83),  // America/Santa_Isabel
    TZ(80340, 66),  // Greenwich
    TZ(80407, 56),  // America/North_Dakota/New_Salem
    TZ(80475, 86),  // Etc/UTC
    TZ(80795, 52),  // Europe/Prague
    TZ(80930, 4),   // Turkey
    TZ(82112, 12),  // Asia/Vientiane
    TZ(82535, 85),  // Pacific/Pago_Pago
    TZ(82814, 77),  // America/Dawson_Creek
    TZ(82979, 47),  // America/St_Lucia
    TZ(83967, 14),  // Asia/Kuala_Lumpur
    TZ(83991, 65),  // America/Port-au-Prince
    TZ(83999, 52),  // Europe/Skopje
    TZ(84174, 53),  // Asia/Shanghai
    TZ(84212, 88),  // Europe/Lisbon
    TZ(84256, 50),  // Africa/Lusaka
    TZ(84506, 66),  // Africa/Abidjan
    TZ(84727, 4),   // Europe/Minsk
    TZ(84886, 66),  // Africa/Bissau
    TZ(85164, 47),  // America/Port_of_Spain
    TZ(85389, 66),  // America/Danmarkshavn
    TZ(85414, 86),  // UTC
    TZ(85480, 12),  // Antarctica/Davis
    TZ(85523, 55),  // America/Edmonton
    TZ(85692, 76),  // W-SU
    TZ(85846, 18),  // Asia/Sakhalin
    TZ(86221, 86),  // Etc/UCT
    TZ(86302, 62),  // Asia/Famagusta
    TZ(86532, 52),  // Europe/Zurich
    TZ(86584, 77),  // America/Phoenix
    TZ(86688, 3),   // Iran
    TZ(87023, 32),  // America/Rio_Branco
    TZ(87125, 18),  // Pacific/Efate
    TZ(87161, 52),  // Europe/Belgrade
    TZ(87539, 77),  // US/Arizona
    TZ(87622, 86),  // Zulu
    TZ(88060, 52),  // Europe/Malta
    TZ(88122, 66),  // Iceland
    TZ(88163, 46),  // America/Sitka
    TZ(88360, 52),  // Europe/Vienna
    TZ(88465, 32),  // America/Porto_Acre
    TZ(88490, 11),  // Asia/Kashgar
    TZ(88620, 28),  // Antarctica/Rothera
    TZ(88645, 67),  // GB
    TZ(88727, 79),  // America/St_Johns
    TZ(88927, 53),  // PRC
    TZ(89286, 55),  // America/Guatemala
    TZ(89417, 26),  // Brazil/DeNoronha
    TZ(89542, 30),  // America/Cuiaba
    TZ(89546, 88),  // Portugal
    TZ(89588, 69),  // US/Hawaii
    TZ(89709, 4),   // Europe/Istanbul
    TZ(89898, 21),  // Asia/Anadyr
    TZ(90037, 28),  // America/Argentina/Ushuaia
    TZ(90756, 55),  // Canada/Saskatchewan
    TZ(91181, 88),  // Atlantic/Canary
    TZ(91189, 6),   // Europe/Ulyanovsk
    TZ(91490, 1),   // Africa/Casablanca
    TZ(91692, 67),  // Europe/Belfast
    TZ(91772, 64),  // Canada/Central
    TZ(91862, 65),  // America/Pangnirtung
    TZ(91952, 6),   // Indian/Reunion
    TZ(92085, 52),  // Europe/Gibraltar
    TZ(92094, 28),  // Antarctica/Palmer
    TZ(92514, 16),  // Australia/LHI
    TZ(92776, 66),  // Africa/Conakry
    TZ(93042, 77),  // Canada/Pacific
    TZ(93088, 65),  // America/Indianapolis
    TZ(93284, 65),  // America/Detroit
    TZ(93355, 52),  // Europe/Luxembourg
    TZ(93396, 8),   // Asia/Katmandu
    TZ(93596, 58),  // Africa/Asmara
    TZ(93696, 14),  // Asia/Ulan_Bator
    TZ(94149, 58),  // Africa/Djibouti
    TZ(94890, 70),  // US/Aleutian
    TZ(95054, 58),  // Africa/Kampala
    TZ(95721, 3),   // Asia/Tehran
    TZ(95776, 22),  // Etc/GMT-13
    TZ(95971, 10),  // Asia/Yangon
    TZ(95995, 68),  // Asia/Hong_Kong
    TZ(96041, 54),  // Cuba
    TZ(96179, 21),  // Etc/GMT-12
    TZ(96275, 56),  // America/Chicago
    TZ(96582, 18),  // Etc/GMT-11
    TZ(96954, 28),  // America/Catamarca
    TZ(96985, 17),  // Etc/GMT-10
    TZ(97502, 28),  // America/Argentina/San_Juan
    TZ(97615, 9),   // Antarctica/Vostok
    TZ(97919, 28),  // America/Paramaribo
    TZ(98219, 28),  // America/Argentina/San_Luis
    TZ(98416, 56),  // US/Indiana-Starke
    TZ(98517, 58),  // Africa/Dar_es_Salaam
    TZ(98585, 55),  // America/Chihuahua
    TZ(98597, 23),  // Etc/GMT-14
    TZ(98633, 66),  // Africa/Freetown
    TZ(99444, 47),  // America/Anguilla
    TZ(101390, 28), // America/Argentina/Rio_Gallegos
    TZ(101645, 45), // Australia/Hobart
    TZ(101795, 48), // America/Thule
    TZ(102337, 12), // Asia/Krasnoyarsk
    TZ(102791, 52), // Atlantic/Jan_Mayen
    TZ(102930, 47), // America/Montserrat
    TZ(103184, 18), // Asia/Srednekolymsk
    TZ(103592, 28), // America/Argentina/Catamarca
    TZ(103692, 65), // America/Iqaluit
    TZ(103778, 28), // America/Argentina/Salta
    TZ(104378, 21), // Pacific/Funafuti
    TZ(104442, 66), // Africa/Bamako
    TZ(104580, 43), // Australia/South
    TZ(104624, 61), // Asia/Beirut
    TZ(104753, 15), // Asia/Yakutsk
    TZ(104761, 58), // Indian/Comoro
    TZ(104861, 86), // Etc/Zulu
    TZ(105205, 52), // Europe/Stockholm
    TZ(105338, 14), // Asia/Irkutsk
    TZ(105625, 59), // Europe/Kaliningrad
    TZ(105721, 9),  // Asia/Yekaterinburg
    TZ(105858, 43), // Australia/Yancowinna
    TZ(106160, 75), // ROK
    TZ(106528, 53), // Asia/Macau
    TZ(106626, 59), // Africa/Tripoli
    TZ(106825, 66), // Africa/Accra
    TZ(106869, 4),  // Asia/Aden
    TZ(107046, 56), // America/Resolute
    TZ(107139, 18), // Pacific/Guadalcanal
    TZ(107371, 87), // Africa/Brazzaville
    TZ(107628, 7),  // Asia/Colombo
    TZ(107767, 65), // America/Indiana/Vincennes
    TZ(107768, 21), // Kwajalein
    TZ(107864, 87), // Africa/Porto-Novo
    TZ(108036, 50), // Africa/Khartoum
    TZ(108336, 11), // Asia/Thimphu
    TZ(108452, 47), // America/Antigua
    TZ(108737, 55), // America/Costa_Rica
    TZ(109134, 65), // America/Kentucky/Monticello
    TZ(109140, 70), // America/Adak
    TZ(109300, 6),  // Europe/Samara
    TZ(109366, 58), // Indian/Antananarivo
    TZ(109384, 53), // ROC
    TZ(109442, 60), // Asia/Gaza
    TZ(109553, 6),  // Asia/Yerevan
    TZ(109654, 52), // Europe/Busingen
    TZ(109928, 4),  // Asia/Bahrain
    TZ(110073, 22), // Pacific/Fakaofo
    TZ(111027, 62), // Europe/Mariehamn
    TZ(111490, 88), // Atlantic/Madeira
    TZ(111553, 66), // GMT-0
    TZ(111752, 64), // America/Winnipeg
    TZ(111961, 21), // Pacific/Nauru
    TZ(111982, 56), // US/Central
    TZ(112590, 78), // US/Mountain
    TZ(112677, 77), // America/Hermosillo
    TZ(112725, 28), // America/Fortaleza
    TZ(112793, 52), // Europe/Vaduz
    TZ(113089, 64), // America/Atikokan
    TZ(113189, 38), // Pacific/Gambier
    TZ(113321, 52), // Europe/Amsterdam
    TZ(113686, 12), // Asia/Novosibirsk
    TZ(114558, 28), // America/Bahia
    TZ(114640, 66), // Africa/Timbuktu
    TZ(114677, 30), // America/Caracas
    TZ(114882, 42), // Australia/North
    TZ(115012, 21), // Pacific/Majuro
    TZ(115100, 70), // America/Atka
    TZ(115163, 1),  // Africa/El_Aaiun
    TZ(115258, 30), // America/Porto_Velho
    TZ(115431, 50), // Africa/Maputo
    TZ(115509, 77), // America/Dawson
    TZ(115586, 76), // Europe/Kirov
    TZ(115638, 90), // Asia/Jayapura
    TZ(115827, 87), // Africa/Lagos
    TZ(115892, 73), // Asia/Calcutta
    TZ(115937, 9),  // Indian/Maldives
    TZ(115982, 28), // America/Santarem
    TZ(116068, 6),  // Europe/Astrakhan
    TZ(116187, 62), // Europe/Uzhgorod
    TZ(116369, 45), // Australia/Tasmania
    TZ(116748, 85), // US/Samoa
    TZ(116929, 56), // America/North_Dakota/Beulah
    TZ(117006, 53), // Asia/Macao
    TZ(117106, 30), // America/La_Paz
    TZ(117287, 67), // Europe/Isle_of_Man
    TZ(117333, 64), // America/Cancun
    TZ(117501, 66), // Etc/GMT
    TZ(117966, 17), // Pacific/Chuuk
    TZ(118797, 14), // Asia/Ulaanbaatar
    TZ(119324, 55), // Mexico/General
    TZ(119507, 53), // Asia/Chungking
    TZ(119649, 56), // America/North_Dakota/Center
    TZ(119682, 62), // Europe/Bucharest
    TZ(119784, 55), // America/Monterrey
    TZ(119916, 91), // Asia/Ujung_Pandang
    TZ(120722, 47), // America/Puerto_Rico
    TZ(120913, 56), // CST6CDT
    TZ(121158, 52), // Europe/Rome
    TZ(121347, 67), // Europe/Jersey
    TZ(121517, 9),  // Asia/Qostanay
    TZ(121549, 66), // Africa/Nouakchott
    TZ(121888, 12), // Asia/Hovd
    TZ(122224, 4),  // Asia/Riyadh
    TZ(122646, 12), // Asia/Saigon
    TZ(122739, 62), // Europe/Nicosia
    TZ(122889, 66), // Etc/Greenwich
    TZ(123018, 62), // Europe/Helsinki
    TZ(123092, 46), // US/Alaska
    TZ(123241, 22), // Pacific/Tongatapu
    TZ(123740, 12), // Indian/Christmas
    TZ(123828, 28), // America/Jujuy
    TZ(124705, 52), // Europe/Budapest
    TZ(124805, 9),  // Asia/Aqtobe
    TZ(125039, 71), // Eire
    TZ(125209, 78), // America/Boise
    TZ(125215, 84), // Africa/Mbabane
    TZ(125541, 10), // Indian/Cocos
    TZ(125667, 47), // America/St_Thomas
    TZ(126172, 46), // America/Yakutat
    TZ(126210, 64), // Jamaica
    TZ(126452, 74), // Japan
    TZ(126750, 65), // EST5EDT
    TZ(126864, 58), // Africa/Asmera
    TZ(127049, 86), // Universal
    TZ(127514, 65), // America/Grand_Turk
    TZ(127721, 44), // Australia/Queensland
    TZ(128004, 65), // America/Nassau
    TZ(128338, 28), // America/Araguaina
    TZ(129136, 56), // America/Matamoros
    TZ(129615, 55), // America/Bahia_Banderas
    TZ(129735, 74), // Asia/Tokyo
    TZ(129758, 17), // Pacific/Yap
    TZ(129777, 28), // America/Montevideo
    TZ(129881, 11), // Asia/Omsk
    TZ(130076, 0),  // Antarctica/Troll
    TZ(130225, 9),  // Asia/Dushanbe
    TZ(130389, 62), // Europe/Tiraspol
    TZ(130413, 28), // America/Coyhaique
    TZ(130572, 66), // Africa/Lome
    TZ(130907, 47), // America/Barbados
    TZ(130930, 22), // Pacific/Enderbury
};

#undef TZ

#endif
