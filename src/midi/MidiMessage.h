#pragma once

#include <QMetaType>
#include <QString>

/// One incoming MIDI event, already parsed by the platform backend. Only
/// what the mapper uses: channel voice messages + clock/transport.
struct MidiMessage
{
    enum Type
    {
        None,
        ControlChange,
        NoteOn,
        NoteOff,
        ProgramChange,
        Clock,
        Start,
        Stop,
        Continue
    };

    Type type = None;
    int channel = 0; // 0–15 (voice messages)
    int number = 0; // CC#, note, program
    int value = 0; // CC value, velocity (NoteOn with velocity 0 arrives as NoteOff)

    bool isVoice() const {
        return type == ControlChange || type == NoteOn || type == NoteOff || type == ProgramChange;
    }

    /// "CC 11 · ch 1 · 64" — for the Mappings window and Learn feedback.
    QString describe() const {
        const QString ch = QString("ch %1").arg(channel + 1);
        switch (type) {
            case ControlChange:
                return QString("CC %1 · %2 · %3").arg(number).arg(ch).arg(value);
            case NoteOn:
                return QString("Note %1 on · %2 · vel %3").arg(number).arg(ch).arg(value);
            case NoteOff:
                return QString("Note %1 off · %2").arg(number).arg(ch);
            case ProgramChange:
                return QString("Program %1 · %2").arg(number).arg(ch);
            case Clock:
                return "Clock";
            case Start:
                return "Start";
            case Stop:
                return "Stop";
            case Continue:
                return "Continue";
            case None:
                break;
        }
        return {};
    }

    /// Parses a raw 1–3 byte message (WinMM delivers these packed).
    static MidiMessage fromBytes(unsigned char status, unsigned char d1, unsigned char d2) {
        MidiMessage m;
        switch (status) {
            case 0xF8:
                m.type = Clock;
                return m;
            case 0xFA:
                m.type = Start;
                return m;
            case 0xFB:
                m.type = Continue;
                return m;
            case 0xFC:
                m.type = Stop;
                return m;
            default:
                break;
        }
        m.channel = status & 0x0F;
        m.number = d1 & 0x7F;
        m.value = d2 & 0x7F;
        switch (status & 0xF0) {
            case 0x80:
                m.type = NoteOff;
                break;
            case 0x90:
                m.type = m.value ? NoteOn : NoteOff;
                break;
            case 0xB0:
                m.type = ControlChange;
                break;
            case 0xC0:
                m.type = ProgramChange;
                m.value = 0;
                break;
            default:
                m.type = None;
                break;
        }
        return m;
    }
};

Q_DECLARE_METATYPE(MidiMessage)
