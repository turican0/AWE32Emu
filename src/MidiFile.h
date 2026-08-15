#pragma once
#include "MidiTypes.h"
#include <string>

// Parser Standard MIDI File (.mid), format 0 and 1.
// TODO (see project TODO list, section 1.1): SysEx events, format 2, edge-case
// variants division = SMPTE instead of ticks-per-quarter.
namespace MidiFile
{
    ParsedSequence Load(const std::string& path);
}
