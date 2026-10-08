// Host stand-ins for the libDaisy peripherals FRIZZ sets up: they hand the twin (board.h) what
// it needs (the audio callback, the LED DMA buffers, the charger's I2C traffic) and otherwise do
// nothing
#pragma once
#include "daisy_core.h"
#include "sys/system.h"

namespace daisy
{
class SaiHandle
{
public:
    struct Config
    {
        enum class Peripheral
        {
            SAI_1,
            SAI_2,
        };
        enum class SampleRate
        {
            SAI_8KHZ,
            SAI_16KHZ,
            SAI_32KHZ,
            SAI_48KHZ,
            SAI_96KHZ,
        };
        enum class BitDepth
        {
            SAI_16BIT,
            SAI_24BIT,
            SAI_32BIT,
        };
        enum class Sync
        {
            MASTER,
            SLAVE,
        };
        enum class Direction
        {
            TRANSMIT,
            RECEIVE,
        };
        Peripheral periph;
        struct
        {
            Pin mclk, fs, sck, sa, sb;
        } pin_config;
        SampleRate sr;
        BitDepth bit_depth;
        Sync a_sync, b_sync;
        Direction a_dir, b_dir;
    };
    void Init(const Config&) {}
};

class AudioHandle
{
public:
    typedef const float* const* InputBuffer;
    typedef float** OutputBuffer;
    typedef void (*AudioCallback)(InputBuffer in, OutputBuffer out, size_t size);

    struct Config
    {
        size_t blocksize = 48;
        SaiHandle::Config::SampleRate samplerate = SaiHandle::Config::SampleRate::SAI_48KHZ;
        float postgain = 1.f;
    };
    void Init(const Config& cfg, SaiHandle&, SaiHandle&) { config_ = cfg; }
    const Config& GetConfig() const { return config_; }

private:
    Config config_;
};

class I2CHandle
{
public:
    struct Config
    {
        enum class Peripheral
        {
            I2C_1,
            I2C_2,
            I2C_3,
            I2C_4,
        };
        enum class Speed
        {
            I2C_100KHZ,
            I2C_400KHZ,
            I2C_1MHZ,
        };
        enum class Mode
        {
            I2C_MASTER,
            I2C_SLAVE,
        };
        Peripheral periph;
        struct
        {
            Pin scl, sda;
        } pin_config;
        Speed speed;
        Mode mode;
        uint8_t address;
    };
    enum class Result
    {
        OK,
        ERR,
    };
    typedef void (*CallbackFunctionPtr)(void* context, Result result);

    Result Init(const Config&) { return Result::OK; }
    Result TransmitBlocking(uint16_t address, uint8_t* data, uint16_t size, uint32_t)
    {
        twin::I2cWrite(address, data, size);
        return Result::OK;
    }
    // the DMA finishes at once: the callback runs before this returns
    Result ReceiveDma(uint16_t address, uint8_t* data, uint16_t size, CallbackFunctionPtr cb,
                      void* context)
    {
        twin::I2cRead(address, data, size);
        if (cb)
            cb(context, Result::OK);
        return Result::OK;
    }
};

class TimerHandle
{
public:
    struct Config
    {
        enum class Peripheral
        {
            TIM_2,
            TIM_3,
            TIM_4,
            TIM_5,
        };
        enum class CounterDir
        {
            UP,
            DOWN,
        };
        Peripheral periph = Peripheral::TIM_2;
        CounterDir dir = CounterDir::UP;
    };
    void Init(const Config&) {}
    void SetPrescaler(uint32_t) {}
    void SetPeriod(uint32_t) {}
    void Start() {}
    void Stop() {}
};

class TimChannel
{
public:
    struct Config
    {
        enum class Channel
        {
            ONE,
            TWO,
            THREE,
            FOUR,
        };
        enum class Mode
        {
            PWM,
        };
        enum class Polarity
        {
            HIGH,
            LOW,
        };
        TimerHandle* tim = nullptr;
        Channel chn = Channel::ONE;
        Mode mode = Mode::PWM;
        Polarity polarity = Polarity::HIGH;
        Pin pin;
    };
    typedef void (*EndCallbackFunctionPtr)(void* context);

    void Init(const Config& cfg) { config_ = cfg; }
    const Config& GetConfig() const { return config_; }
    void Start() {}
    void Stop() {}
    void SetPwm(uint32_t) {}
    void StartDma(void* data, size_t size, EndCallbackFunctionPtr cb = nullptr,
                  void* context = nullptr)
    {
        twin::LedDmaStart(static_cast<int>(config_.chn) + 1, static_cast<const uint32_t*>(data),
                          size, cb, context);
    }

private:
    Config config_;
};

class SdmmcHandler
{
public:
    enum class Speed
    {
        SLOW,
        MEDIUM_SLOW,
        STANDARD,
        FAST,
        VERY_FAST,
    };
    enum class BusWidth
    {
        BITS_1,
        BITS_4,
    };
    struct Config
    {
        Speed speed = Speed::FAST;
        BusWidth width = BusWidth::BITS_4;
        bool clock_powersave = false;
    };
    void Init(const Config&) {}
};
} // namespace daisy

// The SD card: test/host/fatfs.h, a card in memory
#include "fatfs.h"

namespace daisy
{
class FatFSInterface
{
public:
    struct Config
    {
        enum Media : uint8_t
        {
            MEDIA_SD = 0x01,
            MEDIA_USB = 0x02,
        };
        uint8_t media;
    };
    void Init(const uint8_t) {}
    FATFS& GetSDFileSystem() { return fs_; }
    const char* GetSDPath() { return "0:/"; }

private:
    FATFS fs_;
};
} // namespace daisy
