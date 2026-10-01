#pragma once

#include <QDialog>

class MidiInput;
class MidiMapper;
class QCheckBox;
class QLabel;
class QTableWidget;
class QVBoxLayout;

/// MIDI → Mappings…: the device list (enable checkboxes, follow MIDI clock),
/// the mapping table (on, source, channel, number, target, mode, range,
/// delete), per-row Learn, and Load / Save / Defaults for mapping files.
/// Every edit goes straight to the mapper (no OK/Cancel), like the rest of
/// the app's settings.
class MidiMappingsDialog : public QDialog
{
    Q_OBJECT
public:
    MidiMappingsDialog(MidiMapper& mapper, MidiInput* input, QWidget* parent = nullptr);

private:
    void rebuildDevices();
    void rebuildTable();
    void commitRow(int row);

    MidiMapper& mMapper;
    MidiInput* mInput;
    QVBoxLayout* mDeviceBox = nullptr;
    QTableWidget* mTable = nullptr;
    QLabel* mActivity = nullptr;
    bool mRebuilding = false;
};
