#pragma once
#include "MidiTypes.h"
#include <string>

// Parser of XMI files (Miles Sound System / AIL).
// An XMI container is an IFF FORM/CAT structure holding an EVNT chunk with
// the events. The main differences from SMF this parser deals with:
//  - a Note On in XMI carries the note length itself (instead of a separate
//    Note Off) - it becomes a NoteOn + NoteOff pair with a computed absoluteTick
//  - the delta-time encoding differs from SMF VLQ (see the .cpp)
//  - XMI runs at 120 BPM / 60 ticks per quarter note unless the file has a
//    tempo meta event of its own
// TODO (see docs/TODO.md, section 1.2): RBRN (branch/loop body) is not
// handled yet, only noted for later use.
namespace XmiFile
{
    ParsedSequence Load(const std::string& path);
}
