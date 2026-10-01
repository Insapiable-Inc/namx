#pragma once

#include <QMetaType>
#include <QString>

#include "Common.h"

namespace npl
{

/// One incoming MIDI event, already parsed by the platform backend. Only
/// what the mapper uses: channel voice messages + clock/transport.
struct MidiMessage
{
    enum Type
    {
        NONE,
        CONTROL_CHANGE,
        NOTE_ON,
        NOTE_OFF,
        PROGRAM_CHANGE,
        CLOCK,
        START,
        STOP,
        CONTINUE
    };

    Type type = NONE;
    int channel = 0; // 0–15 (voice messages)
    int number = 0; // CC#, note, program
    int value = 0; // CC value, velocity (NoteOn with velocity 0 arrives as NoteOff)

    bool is_voice() const {
        return type == CONTROL_CHANGE || type == NOTE_ON || type == NOTE_OFF ||
               type == PROGRAM_CHANGE;
    }

    /// "CC 11 · ch 1 · 64" — for the Mappings window and Learn feedback.
    QString describe() const {
        const QString ch = QString("ch %1").arg(channel + 1);
        switch (type) {
            case CONTROL_CHANGE:
                return QString("CC %1 · %2 · %3").arg(number).arg(ch).arg(value);
            case NOTE_ON:
                return QString("Note %1 on · %2 · vel %3").arg(number).arg(ch).arg(value);
            case NOTE_OFF:
                return QString("Note %1 off · %2").arg(number).arg(ch);
            case PROGRAM_CHANGE:
                return QString("Program %1 · %2").arg(number).arg(ch);
            case CLOCK:
                return "Clock";
            case START:
                return "Start";
            case STOP:
                return "Stop";
            case CONTINUE:
                return "Continue";
            case NONE:
                break;
        }
        return {};
    }

    /// Parses a raw 1–3 byte message (WinMM delivers these packed).
    static MidiMessage from_bytes(unsigned char status, unsigned char d1, unsigned char d2) {
        MidiMessage m;
        switch (status) {
            case 0xF8:
                m.type = CLOCK;
                return m;
            case 0xFA:
                m.type = START;
                return m;
            case 0xFB:
                m.type = CONTINUE;
                return m;
            case 0xFC:
                m.type = STOP;
                return m;
            default:
                break;
        }
        m.channel = status & 0x0F;
        m.number = d1 & 0x7F;
        m.value = d2 & 0x7F;
        switch (status & 0xF0) {
            case 0x80:
                m.type = NOTE_OFF;
                break;
            case 0x90:
                m.type = m.value ? NOTE_ON : NOTE_OFF;
                break;
            case 0xB0:
                m.type = CONTROL_CHANGE;
                break;
            case 0xC0:
                m.type = PROGRAM_CHANGE;
                m.value = 0;
                break;
            default:
                m.type = NONE;
                break;
        }
        return m;
    }
};

} // namespace npl

Q_DECLARE_METATYPE(npl::MidiMessage)
