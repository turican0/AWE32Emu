#pragma once
#include "MidiTypes.h"
#include <string>

// Parser Standard MIDI File (.mid), format 0 i 1.
// TODO (docs/TODO.md, section 1.1): SysEx events, format 2, the edge case
// division = SMPTE instead of ticks-per-quarter.
namespace MidiFile
{
    ParsedSequence Load(const std::string& path);
}
