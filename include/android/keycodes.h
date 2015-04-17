/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _ANDROID_KEYCODES_H
#define _ANDROID_KEYCODES_H

/******************************************************************
 *
 * IMPORTANT NOTICE:
 *
 *   This file is part of Android's set of stable system headers
 *   exposed by the Android NDK (Native Development Kit).
 *
 *   Third-party source AND binary code relies on the definitions
 *   here to be FROZEN ON ALL UPCOMING PLATFORM RELEASES.
 *
 *   - DO NOT MODIFY ENUMS (EXCEPT IF YOU ADD NEW 32-BIT VALUES)
 *   - DO NOT MODIFY CONSTANTS OR FUNCTIONAL MACROS
 *   - DO NOT CHANGE THE SIGNATURE OF FUNCTIONS IN ANY WAY
 *   - DO NOT CHANGE THE LAYOUT OR SIZE OF STRUCTURES
 */

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Key codes.
 */
enum {
    AKEYCODE_UNKNOWN         = 0,
    AKEYCODE_SOFT_LEFT       = 1,
    AKEYCODE_SOFT_RIGHT      = 2,
    AKEYCODE_HOME            = 3,
    AKEYCODE_BACK            = 4,
    AKEYCODE_CALL            = 5,
    AKEYCODE_ENDCALL         = 6,
    AKEYCODE_0               = 7,
    AKEYCODE_1               = 8,
    AKEYCODE_2               = 9,
    AKEYCODE_3               = 10,
    AKEYCODE_4               = 11,
    AKEYCODE_5               = 12,
    AKEYCODE_6               = 13,
    AKEYCODE_7               = 14,
    AKEYCODE_8               = 15,
    AKEYCODE_9               = 16,
    AKEYCODE_STAR            = 17,
    AKEYCODE_POUND           = 18,
    AKEYCODE_DPAD_UP         = 19,
    AKEYCODE_DPAD_DOWN       = 20,
    AKEYCODE_DPAD_LEFT       = 21,
    AKEYCODE_DPAD_RIGHT      = 22,
    AKEYCODE_DPAD_CENTER     = 23,
    AKEYCODE_VOLUME_UP       = 24,
    AKEYCODE_VOLUME_DOWN     = 25,
    AKEYCODE_POWER           = 26,
    AKEYCODE_CAMERA          = 27,
    AKEYCODE_CLEAR           = 28,
    AKEYCODE_A               = 29,
    AKEYCODE_B               = 30,
    AKEYCODE_C               = 31,
    AKEYCODE_D               = 32,
    AKEYCODE_E               = 33,
    AKEYCODE_F               = 34,
    AKEYCODE_G               = 35,
    AKEYCODE_H               = 36,
    AKEYCODE_I               = 37,
    AKEYCODE_J               = 38,
    AKEYCODE_K               = 39,
    AKEYCODE_L               = 40,
    AKEYCODE_M               = 41,
    AKEYCODE_N               = 42,
    AKEYCODE_O               = 43,
    AKEYCODE_P               = 44,
    AKEYCODE_Q               = 45,
    AKEYCODE_R               = 46,
    AKEYCODE_S               = 47,
    AKEYCODE_T               = 48,
    AKEYCODE_U               = 49,
    AKEYCODE_V               = 50,
    AKEYCODE_W               = 51,
    AKEYCODE_X               = 52,
    AKEYCODE_Y               = 53,
    AKEYCODE_Z               = 54,
    AKEYCODE_COMMA           = 55,
    AKEYCODE_PERIOD          = 56,
    AKEYCODE_ALT_LEFT        = 57,
    AKEYCODE_ALT_RIGHT       = 58,
    AKEYCODE_SHIFT_LEFT      = 59,
    AKEYCODE_SHIFT_RIGHT     = 60,
    AKEYCODE_TAB             = 61,
    AKEYCODE_SPACE           = 62,
    AKEYCODE_SYM             = 63,
    AKEYCODE_EXPLORER        = 64,
    AKEYCODE_ENVELOPE        = 65,
    AKEYCODE_ENTER           = 66,
    AKEYCODE_DEL             = 67,
    AKEYCODE_GRAVE           = 68,
    AKEYCODE_MINUS           = 69,
    AKEYCODE_EQUALS          = 70,
    AKEYCODE_LEFT_BRACKET    = 71,
    AKEYCODE_RIGHT_BRACKET   = 72,
    AKEYCODE_BACKSLASH       = 73,
    AKEYCODE_SEMICOLON       = 74,
    AKEYCODE_APOSTROPHE      = 75,
    AKEYCODE_SLASH           = 76,
    AKEYCODE_AT              = 77,
    AKEYCODE_NUM             = 78,
    AKEYCODE_HEADSETHOOK     = 79,
    AKEYCODE_FOCUS           = 80,   // *Camera* focus
    AKEYCODE_PLUS            = 81,
    AKEYCODE_MENU            = 82,
    AKEYCODE_NOTIFICATION    = 83,
    AKEYCODE_SEARCH          = 84,
    AKEYCODE_MEDIA_PLAY_PAUSE= 85,
    AKEYCODE_MEDIA_STOP      = 86,
    AKEYCODE_MEDIA_NEXT      = 87,
    AKEYCODE_MEDIA_PREVIOUS  = 88,
    AKEYCODE_MEDIA_REWIND    = 89,
    AKEYCODE_MEDIA_FAST_FORWARD = 90,
    AKEYCODE_MUTE            = 91,
    AKEYCODE_PAGE_UP         = 92,
    AKEYCODE_PAGE_DOWN       = 93,
    AKEYCODE_PICTSYMBOLS     = 94,
    AKEYCODE_SWITCH_CHARSET  = 95,
    AKEYCODE_BUTTON_A        = 96,
    AKEYCODE_BUTTON_B        = 97,
    AKEYCODE_BUTTON_C        = 98,
    AKEYCODE_BUTTON_X        = 99,
    AKEYCODE_BUTTON_Y        = 100,
    AKEYCODE_BUTTON_Z        = 101,
    AKEYCODE_BUTTON_L1       = 102,
    AKEYCODE_BUTTON_R1       = 103,
    AKEYCODE_BUTTON_L2       = 104,
    AKEYCODE_BUTTON_R2       = 105,
    AKEYCODE_BUTTON_THUMBL   = 106,
    AKEYCODE_BUTTON_THUMBR   = 107,
    AKEYCODE_BUTTON_START    = 108,
    AKEYCODE_BUTTON_SELECT   = 109,
    AKEYCODE_BUTTON_MODE     = 110,
    AKEYCODE_ESCAPE          = 111,
    AKEYCODE_FORWARD_DEL     = 112,
    AKEYCODE_CTRL_LEFT       = 113,
    AKEYCODE_CTRL_RIGHT      = 114,
    AKEYCODE_CAPS_LOCK       = 115,
    AKEYCODE_SCROLL_LOCK     = 116,
    AKEYCODE_META_LEFT       = 117,
    AKEYCODE_META_RIGHT      = 118,
    AKEYCODE_FUNCTION        = 119,
    AKEYCODE_SYSRQ           = 120,
    AKEYCODE_BREAK           = 121,
    AKEYCODE_MOVE_HOME       = 122,
    AKEYCODE_MOVE_END        = 123,
    AKEYCODE_INSERT          = 124,
    AKEYCODE_FORWARD         = 125,
    AKEYCODE_MEDIA_PLAY      = 126,
    AKEYCODE_MEDIA_PAUSE     = 127,
    AKEYCODE_MEDIA_CLOSE     = 128,
    AKEYCODE_MEDIA_EJECT     = 129,
    AKEYCODE_MEDIA_RECORD    = 130,
    AKEYCODE_F1              = 131,
    AKEYCODE_F2              = 132,
    AKEYCODE_F3              = 133,
    AKEYCODE_F4              = 134,
    AKEYCODE_F5              = 135,
    AKEYCODE_F6              = 136,
    AKEYCODE_F7              = 137,
    AKEYCODE_F8              = 138,
    AKEYCODE_F9              = 139,
    AKEYCODE_F10             = 140,
    AKEYCODE_F11             = 141,
    AKEYCODE_F12             = 142,
    AKEYCODE_NUM_LOCK        = 143,
    AKEYCODE_NUMPAD_0        = 144,
    AKEYCODE_NUMPAD_1        = 145,
    AKEYCODE_NUMPAD_2        = 146,
    AKEYCODE_NUMPAD_3        = 147,
    AKEYCODE_NUMPAD_4        = 148,
    AKEYCODE_NUMPAD_5        = 149,
    AKEYCODE_NUMPAD_6        = 150,
    AKEYCODE_NUMPAD_7        = 151,
    AKEYCODE_NUMPAD_8        = 152,
    AKEYCODE_NUMPAD_9        = 153,
    AKEYCODE_NUMPAD_DIVIDE   = 154,
    AKEYCODE_NUMPAD_MULTIPLY = 155,
    AKEYCODE_NUMPAD_SUBTRACT = 156,
    AKEYCODE_NUMPAD_ADD      = 157,
    AKEYCODE_NUMPAD_DOT      = 158,
    AKEYCODE_NUMPAD_COMMA    = 159,
    AKEYCODE_NUMPAD_ENTER    = 160,
    AKEYCODE_NUMPAD_EQUALS   = 161,
    AKEYCODE_NUMPAD_LEFT_PAREN = 162,
    AKEYCODE_NUMPAD_RIGHT_PAREN = 163,
    AKEYCODE_VOLUME_MUTE     = 164,
    AKEYCODE_INFO            = 165,
    AKEYCODE_CHANNEL_UP      = 166,
    AKEYCODE_CHANNEL_DOWN    = 167,
    AKEYCODE_ZOOM_IN         = 168,
    AKEYCODE_ZOOM_OUT        = 169,
    AKEYCODE_TV              = 170,
    AKEYCODE_WINDOW          = 171,
    AKEYCODE_GUIDE           = 172,
    AKEYCODE_DVR             = 173,
    AKEYCODE_BOOKMARK        = 174,
    AKEYCODE_CAPTIONS        = 175,
    AKEYCODE_SETTINGS        = 176,
    AKEYCODE_TV_POWER        = 177,
    AKEYCODE_TV_INPUT        = 178,
    AKEYCODE_STB_POWER       = 179,
    AKEYCODE_STB_INPUT       = 180,
    AKEYCODE_AVR_POWER       = 181,
    AKEYCODE_AVR_INPUT       = 182,
    AKEYCODE_PROG_RED        = 183,
    AKEYCODE_PROG_GREEN      = 184,
    AKEYCODE_PROG_YELLOW     = 185,
    AKEYCODE_PROG_BLUE       = 186,
    AKEYCODE_APP_SWITCH      = 187,
    AKEYCODE_BUTTON_1        = 188,
    AKEYCODE_BUTTON_2        = 189,
    AKEYCODE_BUTTON_3        = 190,
    AKEYCODE_BUTTON_4        = 191,
    AKEYCODE_BUTTON_5        = 192,
    AKEYCODE_BUTTON_6        = 193,
    AKEYCODE_BUTTON_7        = 194,
    AKEYCODE_BUTTON_8        = 195,
    AKEYCODE_BUTTON_9        = 196,
    AKEYCODE_BUTTON_10       = 197,
    AKEYCODE_BUTTON_11       = 198,
    AKEYCODE_BUTTON_12       = 199,
    AKEYCODE_BUTTON_13       = 200,
    AKEYCODE_BUTTON_14       = 201,
    AKEYCODE_BUTTON_15       = 202,
    AKEYCODE_BUTTON_16       = 203,
    AKEYCODE_LANGUAGE_SWITCH = 204,
    AKEYCODE_MANNER_MODE     = 205,
    AKEYCODE_3D_MODE         = 206,
    AKEYCODE_CONTACTS        = 207,
    AKEYCODE_CALENDAR        = 208,
    AKEYCODE_MUSIC           = 209,
    AKEYCODE_CALCULATOR      = 210,
    AKEYCODE_ZENKAKU_HANKAKU = 211,
    AKEYCODE_EISU            = 212,
    AKEYCODE_MUHENKAN        = 213,
    AKEYCODE_HENKAN          = 214,
    AKEYCODE_KATAKANA_HIRAGANA = 215,
    AKEYCODE_YEN             = 216,
    AKEYCODE_RO              = 217,
    AKEYCODE_KANA            = 218,
    AKEYCODE_ASSIST          = 219,
    AKEYCODE_BRIGHTNESS_DOWN = 220,
    AKEYCODE_BRIGHTNESS_UP   = 221,
    AKEYCODE_MEDIA_AUDIO_TRACK = 222,
    // MStar Android Patch Begin
    // Common section, range 251-300
    AKEYCODE_SOUND_MODE                 = 251,
    AKEYCODE_PICTURE_MODE               = 252,
    AKEYCODE_ASPECT_RATIO               = 253,
    AKEYCODE_CHANNEL_RETURN             = 254,
    AKEYCODE_SLEEP                      = 255,
    AKEYCODE_EPG                        = 256,
    AKEYCODE_LIST                       = 257,
    AKEYCODE_SUBTITLE                   = 258,
    AKEYCODE_FAVORITE                   = 259,
    AKEYCODE_MTS                        = 260,
    AKEYCODE_FREEZE                     = 261,
    AKEYCODE_TTX                        = 262,
    AKEYCODE_CC                         = 263,
    AKEYCODE_TV_SETTING                 = 264,
    AKEYCODE_SCREENSHOT                 = 265,
    AKEYCODE_CLOUD                      = 266,
    AKEYCODE_VOICE                      = 267,
    AKEYCODE_USB                        = 268,
    AKEYCODE_HDMI                       = 269,
    AKEYCODE_DISPLAY_MODE               = 270,
    AKEYCODE_SONG_SYSTEM                = 271,
    AKEYCODE_GINGA_BACK                 = 272,
    AKEYCODE_NETFLIX                    = 273,
    AKEYCODE_AMAZONE                    = 274,
    // Mstar section, range 301-400
    AKEYCODE_MSTAR_BALANCE              = 301,
    AKEYCODE_MSTAR_INDEX                = 302,
    AKEYCODE_MSTAR_HOLD                 = 303,
    AKEYCODE_MSTAR_UPDATE               = 304,
    AKEYCODE_MSTAR_REVEAL               = 305,
    AKEYCODE_MSTAR_SUBCODE              = 306,
    AKEYCODE_MSTAR_SIZE                 = 307,
    AKEYCODE_MSTAR_CLOCK                = 308,
    AKEYCODE_MSTAR_STORE_UP             = 309,
    AKEYCODE_MSTAR_TRIANGLE_UP          = 310,
    AKEYCODE_MSTAR_MOVIE                = 311,
    AKEYCODE_MSTAR_FILE                 = 312,
    AKEYCODE_MSTAR_STAR_PLUS            = 313,
    AKEYCODE_MSTAR_AUDIO_TRACK          = 314,
    AKEYCODE_MSTAR_OPTIONAL_TIME        = 315,
    AKEYCODE_MSTAR_LOOP                 = 316,
    AKEYCODE_MSTAR_INBOX                = 317,
    AKEYCODE_MSTAR_VVOIP                = 318,
    AKEYCODE_MSTAR_PVR_BROWSER          = 319,
    // Konka section, range 501-600
    AKEYCODE_KONKA_YPBPR                = 501,
    AKEYCODE_KONKA_THREEPOINT_LOONPRESS = 502,
    AKEYCODE_KONKA_THREEPOINT_COLLECT   = 503,
    AKEYCODE_KONKA_THREEPOINT_DISPERSE  = 504,
    AKEYCODE_KONKA_VOICESWITCH          = 505,
    AKEYCODE_KONKA_FLYIMEFINGER_SELECT  = 506,
    AKEYCODE_KONKA_FLYIMEFINGER_CANCEL  = 507,
    AKEYCODE_KONKA_SOUNDOUTPUT_ENABLE   = 508,
    AKEYCODE_KONKA_SOUNDOUTPUT_DISABLE  = 509,
    AKEYCODE_KONKA_BESTV_EXIT           = 510,
    AKEYCODE_KONKA_BESTV_FORWARD        = 511,
    AKEYCODE_KONKA_BESTV_BACKWARD       = 512,
    AKEYCODE_KONKA_ENTER_FACTORY        = 513,
    AKEYCODE_KONKA_FACTORY_BAKE_TV      = 514,
    // Haier section, range  401-500
    AKEYCODE_HAIER_TASK                 = 401,
    AKEYCODE_HAIER_TOOLS                = 402,
    AKEYCODE_HAIER_POWERSLEEP           = 403,
    AKEYCODE_HAIER_WAKEUP               = 404,
    AKEYCODE_HAIER_UNMUTE               = 405,
    AKEYCODE_HAIER_CLEANSEARCH          = 406,
    // Skyworth section, range 601-700

    // Tcl section, range 4001-4100
    AKEYCODE_TCL_MITV                   = 4001,
    AKEYCODE_TCL_USB_MENU               = 4002,
    AKEYCODE_TCL_SWING_R1               = 4003,
    AKEYCODE_TCL_SWING_R2               = 4004,
    AKEYCODE_TCL_SWING_R3               = 4005,
    AKEYCODE_TCL_SWING_R4               = 4006,
    AKEYCODE_TCL_SWING_L1               = 4007,
    AKEYCODE_TCL_SWING_L2               = 4008,
    AKEYCODE_TCL_SWING_L3               = 4009,
    AKEYCODE_TCL_SWING_L4               = 4010,
    AKEYCODE_TCL_WIDGET                 = 4011,
    AKEYCODE_TCL_VGR_LEFT               = 4012,
    AKEYCODE_TCL_VGR_RIGHT              = 4013,
    AKEYCODE_TCL_VGR_TAP                = 4014,
    AKEYCODE_TCL_VGR_WAVE               = 4015,
    AKEYCODE_TCL_VGR_WAVE_LEFT          = 4016,
    AKEYCODE_TCL_VGR_WAVE_RIGHT         = 4017,
    AKEYCODE_TCL_VGR_ACTIVE             = 4018,
    AKEYCODE_TCL_VGR_DEACTIVE           = 4019,
    AKEYCODE_TCL_BODY_SENSOR            = 4020,
    AKEYCODE_TCL_CIRCLE_CLOCKWISE       = 4021,
    AKEYCODE_TCL_CIRCLE_CTR_CLOCKWISE   = 4022,
    AKEYCODE_TCL_GESTURE_X              = 4023,
    AKEYCODE_TCL_GESTURE_ALPHA          = 4024,
    AKEYCODE_TCL_GESTURE_MUTE           = 4025,
    AKEYCODE_TCL_UP                     = 4026,
    AKEYCODE_TCL_DOWN                   = 4027,
    AKEYCODE_TCL_LEFT                   = 4028,
    AKEYCODE_TCL_RIGHT                  = 4029,
    AKEYCODE_TCL_UP_LEFT                = 4030,
    AKEYCODE_TCL_UP_RIGHT               = 4031,
    AKEYCODE_TCL_DOWN_LEFT              = 4032,
    AKEYCODE_TCL_DOWN_RIGHT             = 4033,
    // Changhong section, range 4101-4200
    AKEYCODE_CHANGHONGIR_MUTE           = 4101,
    AKEYCODE_CHANGHONGIR_INPUT          = 4102,
    AKEYCODE_CHANGHONGIR_DEL            = 4103,
    AKEYCODE_CHANGHONGIR_MENU           = 4104,
    AKEYCODE_CHANGHONGIR_CORN           = 4105,
    AKEYCODE_CHANGHONGIR_OK             = 4106,
    AKEYCODE_CHANGHONGIR_FLCK_FU        = 4107,
    AKEYCODE_CHANGHONGIR_FLCK_FD        = 4108,
    AKEYCODE_CHANGHONGIR_FLCK_FL        = 4109,
    AKEYCODE_CHANGHONGIR_FLCK_FR        = 4110,
    AKEYCODE_CHANGHONGIR_FLCK_SU        = 4111,
    AKEYCODE_CHANGHONGIR_FLCK_SD        = 4112,
    AKEYCODE_CHANGHONGIR_FLCK_SL        = 4113,
    AKEYCODE_CHANGHONGIR_FLCK_SR        = 4114,
    AKEYCODE_CHANGHONGIR_PINCH          = 4115,
    AKEYCODE_CHANGHONGIR_SPREAD         = 4116,
    AKEYCODE_CHANGHONGIR_VOICE          = 4117,
    AKEYCODE_CHANGHONGIR_HAND           = 4118,
    AKEYCODE_CHANGHONGIR_3D             = 4119,
    AKEYCODE_CHANGHONGIR_HELP           = 4120,
    AKEYCODE_CHANGHONGIR_APP            = 4121,
    AKEYCODE_CHANGHONGIR_MOUSE          = 4122,
    AKEYCODE_CHANGHONGIR_EPG            = 4123,
    AKEYCODE_CHANGHONGIR_HOME           = 4124,
    AKEYCODE_CHANGHONGIR_SETTINGS       = 4125,
    // Hisense section, range 4201-4300
    AKEYCODE_HISENSE_G_SENSOR           = 4201,
    AKEYCODE_HISENSE_LOW_BATTERY        = 4202,
    AKEYCODE_HISENSE_SLIDEUP            = 4203,
    AKEYCODE_HISENSE_SLIDEDOWN          = 4204,
    AKEYCODE_HISENSE_SLIDELEFT          = 4205,
    AKEYCODE_HISENSE_SLIDERIGHT         = 4206,
    AKEYCODE_HISENSE_RAPID_SLIDEUP      = 4207,
    AKEYCODE_HISENSE_RAPID_SLIDEDOWN    = 4208,
    AKEYCODE_HISENSE_RAPID_SLIDELEFT    = 4209,
    AKEYCODE_HISENSE_RAPID_SLIDERIGHT   = 4210,
    AKEYCODE_HISENSE_FAC_NEC_M          = 4211,
    AKEYCODE_HISENSE_FAC_NEC_IP         = 4212,
    AKEYCODE_HISENSE_FAC_NEC_SAVE       = 4213,
    AKEYCODE_HISENSE_FAC_NEC_3D         = 4214,
    AKEYCODE_HISENSE_FAC_NEC_PC         = 4215,
    AKEYCODE_HISENSE_FAC_NEC_LOGO       = 4216,
    AKEYCODE_HISENSE_FAC_NEC_YPBPR      = 4217,
    AKEYCODE_HISENSE_FAC_NEC_HDMI       = 4218,
    AKEYCODE_HISENSE_FAC_NEC_F1         = 4219,
    AKEYCODE_HISENSE_FAC_NEC_F2         = 4220,
    AKEYCODE_HISENSE_FAC_NEC_F3         = 4221,
    AKEYCODE_HISENSE_FAC_NEC_F4         = 4222,
    AKEYCODE_HISENSE_FAC_NEC_F5         = 4223,
    AKEYCODE_HISENSE_FAC_NEC_F6         = 4224,
    AKEYCODE_HISENSE_FAC_NEC_F7         = 4225,
    AKEYCODE_HISENSE_FAC_NEC_OK         = 4226,
    AKEYCODE_HISENSE_FAC_NEC_MAC        = 4227,
    AKEYCODE_HISENSE_FAC_NEC_AV         = 4228,
    AKEYCODE_HISENSE_FAC_NEC_PATTERN    = 4229,
    AKEYCODE_HISENSE_FAC_NEC_AGING      = 4230,
    AKEYCODE_HISENSE_FAC_NEC_BALANCE    = 4231,
    AKEYCODE_HISENSE_FAC_NEC_ADC        = 4232,
    AKEYCODE_HISENSE_FAC_NEC_RDRV_INCREASE = 4233,
    AKEYCODE_HISENSE_FAC_NEC_RDRV_DECREASE = 4234,
    AKEYCODE_HISENSE_FAC_NEC_GDRV_INCREASE = 4235,
    AKEYCODE_HISENSE_FAC_NEC_GDRV_DECREASE = 4236,
    AKEYCODE_HISENSE_FAC_NEC_BDRV_INCREASE = 4237,
    AKEYCODE_HISENSE_FAC_NEC_BDRV_DECREASE = 4238,
    AKEYCODE_HISENSE_FAC_NEC_RCUT_INCREASE = 4239,
    AKEYCODE_HISENSE_FAC_NEC_RCUT_DECREASE = 4240,
    AKEYCODE_HISENSE_FAC_NEC_GCUT_INCREASE = 4241,
    AKEYCODE_HISENSE_FAC_NEC_GCUT_DECREASE = 4242,
    AKEYCODE_HISENSE_FAC_NEC_BCUT_INCREASE = 4243,
    AKEYCODE_HISENSE_FAC_NEC_BCUT_DECREASE = 4244,
    AKEYCODE_HISENSE_PRODUCT_SCAN_START = 4245,
    AKEYCODE_HISENSE_PRODUCT_SCAN_OVER  = 4246,
    AKEYCODE_HISENSE_TEST_BROAD_TV      = 4247,
    AKEYCODE_HISENSE_TEST_BROAD_DTV     = 4248,
    AKEYCODE_HISENSE_TEST_BROAD_AV1     = 4249,
    AKEYCODE_HISENSE_TEST_BROAD_AV2     = 4250,
    AKEYCODE_HISENSE_TEST_BROAD_AV3     = 4251,
    AKEYCODE_HISENSE_TEST_BROAD_SVIDEO1 = 4252,
    AKEYCODE_HISENSE_TEST_BROAD_SVIDEO2 = 4253,
    AKEYCODE_HISENSE_TEST_BROAD_SVIDEO3 = 4254,
    AKEYCODE_HISENSE_TEST_BROAD_SCART1  = 4255,
    AKEYCODE_HISENSE_TEST_BROAD_SCART2  = 4256,
    AKEYCODE_HISENSE_TEST_BROAD_SCART3  = 4257,
    AKEYCODE_HISENSE_TEST_BOARD_YPBPR1  = 4258,
    AKEYCODE_HISENSE_TEST_BOARD_YPBPR2  = 4259,
    AKEYCODE_HISENSE_TEST_BOARD_YPBPR3  = 4260,
    AKEYCODE_HISENSE_TEST_BOARD_VGA     = 4261,
    AKEYCODE_HISENSE_TEST_BOARD_HDMI1   = 4262,
    AKEYCODE_HISENSE_TEST_BOARD_HDMI2   = 4263,
    AKEYCODE_HISENSE_TEST_BOARD_HDMI3   = 4264,
    AKEYCODE_HISENSE_TEST_BOARD_HDMI4   = 4265,
    AKEYCODE_HISENSE_TEST_BOARD_HDMI5   = 4266,
    AKEYCODE_HISENSE_TEST_BOARD_DMP     = 4267,
    AKEYCODE_HISENSE_TEST_BOARD_EMP     = 4268,
    AKEYCODE_HISENSE_TEST_BOARD_AUTOCOLOR = 4269,
    AKEYCODE_HISENSE_TEST_BOARD_SAVE    = 4270,
    AKEYCODE_HISENSE_TEST_BOARD_TELITEXT = 4271,
    AKEYCODE_HISENSE_TEST_BOARD_SAPL    = 4272,
    AKEYCODE_HISENSE_TEST_BOARD_VCHIP   = 4273,
    AKEYCODE_HISENSE_TEST_BOARD_CCD     = 4274,
    AKEYCODE_HISENSE_TEST_BOARD_BTSC    = 4275,
    AKEYCODE_HISENSE_TEST_BOARD_FAC_OK  = 4276,
    // MStar Android Patch End

    // NOTE: If you add a new keycode here you must also add it to several other files.
    //       Refer to frameworks/base/core/java/android/view/KeyEvent.java for the full list.
};

#ifdef __cplusplus
}
#endif

#endif // _ANDROID_KEYCODES_H
