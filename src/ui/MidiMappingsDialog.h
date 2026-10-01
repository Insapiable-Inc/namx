#pragma once

#include <QDialog>

#include "Common.h"

class QCheckBox;
class QLabel;
class QTableWidget;
class QVBoxLayout;

namespace npl
{

class MidiInput;
class MidiMapper;

// MIDI → Mappings…: the device list (enable checkboxes, follow MIDI clock),
// the mapping table (on, source, channel, number, target, mode, range,
// delete), per-row Learn, and Load / Save / Defaults for mapping files.
// Every edit goes straight to the mapper (no OK/Cancel), like the rest of
// the app's settings.
class MidiMappingsDialog : public QDialog
{
    Q_OBJECT
public:
    MidiMappingsDialog(MidiMapper& new_mapper, MidiInput* new_input, QWidget* parent = nullptr);

private:
    void rebuild_devices();
    void rebuild_table();
    void commit_row(int row);
    void add_mapping();
    void delete_mapping(int row);
    void load_mappings();
    void save_mappings();
    // A 0–100 % spin box for the Min/Max column of `row`.
    void add_percent_cell(int row, int column, double value);

    MidiMapper& mapper;
    MidiInput* input;
    QVBoxLayout* device_box = nullptr;
    QTableWidget* table = nullptr;
    QLabel* activity = nullptr;
    bool rebuilding = false;
};

} // namespace npl
