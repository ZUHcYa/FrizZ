// Host stand-in for libDaisy's UartHandler: receiving only, from the twin (board.h)
#pragma once
#include "daisy_core.h"

namespace daisy
{
class UartHandler
{
public:
    struct Config
    {
        enum class Peripheral
        {
            USART_1,
            USART_2,
            USART_3,
            UART_4,
            UART_5,
            USART_6,
            UART_7,
            UART_8,
            LPUART_1,
        };
        enum class StopBits
        {
            BITS_0_5,
            BITS_1,
            BITS_1_5,
            BITS_2,
        };
        enum class Parity
        {
            NONE,
            EVEN,
            ODD,
        };
        enum class Mode
        {
            RX,
            TX,
            TX_RX,
        };
        enum class WordLength
        {
            BITS_7,
            BITS_8,
            BITS_9,
        };
        struct
        {
            dsy_gpio_pin tx;
            dsy_gpio_pin rx;
        } pin_config;
        Peripheral periph = Peripheral::USART_1;
        StopBits stopbits = StopBits::BITS_1;
        Parity parity = Parity::NONE;
        Mode mode = Mode::TX_RX;
        WordLength wordlength = WordLength::BITS_8;
        uint32_t baudrate = 31250;
    };

    enum class Result
    {
        OK,
        ERR,
    };

    typedef void (*CircularRxCallbackFunctionPtr)(uint8_t* data, size_t size, void* context,
                                                  Result result);

    Result Init(const Config& config)
    {
        config_ = config;
        return Result::OK;
    }

    Result DmaListenStart(uint8_t*, size_t, CircularRxCallbackFunctionPtr cb, void* context)
    {
        cb_ = cb;
        context_ = context;
        listening_ = true;
        twin::UartListen(&UartHandler::Rx, this);
        return Result::OK;
    }

    bool IsListening() const { return listening_; }
    Result PollTx(uint8_t*, size_t) { return Result::OK; }

private:
    static void Rx(uint8_t* data, size_t size, void* context)
    {
        UartHandler* self = static_cast<UartHandler*>(context);
        if (self->cb_)
            self->cb_(data, size, self->context_, Result::OK);
    }

    Config config_;
    CircularRxCallbackFunctionPtr cb_ = nullptr;
    void* context_ = nullptr;
    bool listening_ = false;
};
} // namespace daisy
