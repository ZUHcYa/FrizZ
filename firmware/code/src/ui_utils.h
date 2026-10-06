/** @file ui_utils.h
 *  @brief The LED canvas's clear/flush functions required by libDaisy's UI framework
 *  Actual LED output in temp_led_stuff.h
 */
#pragma once
#include "daisy.h"

/** The pages draw their LEDs themselves (fill_led_data), so these do nothing */
void FlushLeds(const daisy::UiCanvasDescriptor&) {}
void ClearLeds(const daisy::UiCanvasDescriptor&) {}