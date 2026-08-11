#include "AudioDevicePanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSettings>

#include "AudioEngine.h"

AudioDevicePanel::AudioDevicePanel(AudioEngine& engine, QWidget* parent)
    : QWidget(parent)
    , mEngine(engine)
{
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(4, 0, 4, 4);
    row->setSpacing(8);

    auto* inputLabel = new QLabel("Input:");
    inputLabel->setObjectName("deviceLabel");
    mCbxInput = new QComboBox;
    mCbxInput->setMinimumWidth(180);
    mCbxInput->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    auto* outputLabel = new QLabel("Output:");
    outputLabel->setObjectName("deviceLabel");
    mCbxOutput = new QComboBox;
    mCbxOutput->setMinimumWidth(180);
    mCbxOutput->setSizeAdjustPolicy(QComboBox::AdjustToContents);

    row->addWidget(inputLabel);
    row->addWidget(mCbxInput, 1);
    row->addSpacing(16);
    row->addWidget(outputLabel);
    row->addWidget(mCbxOutput, 1);

    connect(mCbxInput,  QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioDevicePanel::onSelectionChanged);
    connect(mCbxOutput, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AudioDevicePanel::onSelectionChanged);
}

void AudioDevicePanel::refresh()
{
    mCbxInput->blockSignals(true);
    mCbxOutput->blockSignals(true);

    mCbxInput->clear();
    mCbxOutput->clear();

    const auto inputs  = mEngine.enumerateInputDevices();
    const auto outputs = mEngine.enumerateOutputDevices();

    // If only "Default" is available (JACK or single entry), disable combos.
    const bool onlyDefault = (inputs.size() <= 1 && outputs.size() <= 1);

    for (const auto& d : inputs)
        mCbxInput->addItem(QString::fromStdString(d.name),
                           QString::fromStdString(d.id));
    for (const auto& d : outputs)
        mCbxOutput->addItem(QString::fromStdString(d.name),
                            QString::fromStdString(d.id));

    mCbxInput->setEnabled(!onlyDefault);
    mCbxOutput->setEnabled(!onlyDefault);

    QSettings s;
    const int idxIn = mCbxInput->findData(s.value("audio/inputDeviceId").toString());
    if (idxIn >= 0) mCbxInput->setCurrentIndex(idxIn);

    const int idxOut = mCbxOutput->findData(s.value("audio/outputDeviceId").toString());
    if (idxOut >= 0) mCbxOutput->setCurrentIndex(idxOut);

    mCbxInput->blockSignals(false);
    mCbxOutput->blockSignals(false);
}

QString AudioDevicePanel::inputDeviceId() const
{
    return mCbxInput->currentData().toString();
}

QString AudioDevicePanel::outputDeviceId() const
{
    return mCbxOutput->currentData().toString();
}

void AudioDevicePanel::onSelectionChanged()
{
    if (mChanging) return;
    mChanging = true;
    QSettings s;
    s.setValue("audio/inputDeviceId",  inputDeviceId());
    s.setValue("audio/outputDeviceId", outputDeviceId());
    emit deviceSelectionChanged();
    mChanging = false;
}
