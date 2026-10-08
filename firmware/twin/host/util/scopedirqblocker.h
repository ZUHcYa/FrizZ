// Host stand-in: the twin runs the audio callback and main() in turn, never at once
#pragma once
namespace daisy
{
class ScopedIrqBlocker
{
public:
    ScopedIrqBlocker() {}
    ~ScopedIrqBlocker() {}
};
} // namespace daisy
