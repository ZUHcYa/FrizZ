// Host stand-in for libDaisy's daisy.h. libDaisy's own UI, Switch, 4021 and MIDI code is
// compiled unchanged (copied next to these by build.sh); what touches the chip is periph.h
#pragma once
#include "daisy_core.h"
#include "sys/system.h"
#include "per/gpio.h"
#include "per/uart.h"
#include "dev/sr_4021.h"
#include "hid/switch.h"
#include "hid/midi.h"
#include "ui/UI.h"
#include "util/CpuLoadMeter.h"
#include "periph.h"
