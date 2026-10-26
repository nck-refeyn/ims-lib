// Copyright 2026 Isomet (UK) Ltd. All rights reserved.
//
// EEPROM access through the simulated synthesiser.

#include "SimFixture.h"
#include "EEPROM.h"

#include <gtest/gtest.h>

using namespace iMS;
using namespace iMS::sim;
using namespace std::chrono_literals;

namespace {

    class EEPROMTest : public ims_test::SimSystemTest {};

}

TEST_F(EEPROMTest, ReadReturnsDeviceContents)
{
    std::vector<std::uint8_t> contents(300);
    for (std::size_t i = 0; i < contents.size(); i++) contents[i] = static_cast<std::uint8_t>(i * 7);
    dev().WriteMemory(HostReport::Actions::SYNTH_EEPROM, 1000, contents);

    EEPROM ee(ims());
    ims_test::EventRecorder rec;
    ee.EEPROMEventSubscribe(EEPROMEvents::EEPROM_READ_DONE, &rec);
    ee.EEPROMEventSubscribe(EEPROMEvents::EEPROM_ACCESS_FAILED, &rec);
    ASSERT_NE(ee.ReadEEPROM(EEPROM::TARGET::SYNTH, 1000, contents.size()), -1);
    ASSERT_EQ(rec.WaitForAny({ EEPROMEvents::EEPROM_READ_DONE, EEPROMEvents::EEPROM_ACCESS_FAILED }, 2s), EEPROMEvents::EEPROM_READ_DONE);
    EXPECT_EQ(ee.EEPROMData<std::vector<std::uint8_t>>(), contents);
    ee.EEPROMEventUnsubscribe(EEPROMEvents::EEPROM_READ_DONE, &rec);
    ee.EEPROMEventUnsubscribe(EEPROMEvents::EEPROM_ACCESS_FAILED, &rec);
}

// Regression: ReadEEPROM recorded each request's handle after SendMsg returned, and marked the read
// complete after the loop. A response handled before either step was ignored, the read never
// completed, and IMSSystem::Initialise (which reads the file system table this way) hung. Seen in
// about 1 in 400 simulated start-ups once responses were dispatched without delay.
TEST_F(EEPROMTest, RepeatedReadsAllComplete)
{
    for (int i = 0; i < 300; i++) {
        EEPROM ee(ims());
        ims_test::EventRecorder rec;
        ee.EEPROMEventSubscribe(EEPROMEvents::EEPROM_READ_DONE, &rec);
        ee.EEPROMEventSubscribe(EEPROMEvents::EEPROM_ACCESS_FAILED, &rec);
        ASSERT_NE(ee.ReadEEPROM(EEPROM::TARGET::SYNTH, 0, 128), -1) << "read " << i;
        ASSERT_EQ(rec.WaitForAny({ EEPROMEvents::EEPROM_READ_DONE, EEPROMEvents::EEPROM_ACCESS_FAILED }, 2s),
            EEPROMEvents::EEPROM_READ_DONE) << "read " << i;
        ee.EEPROMEventUnsubscribe(EEPROMEvents::EEPROM_READ_DONE, &rec);
        ee.EEPROMEventUnsubscribe(EEPROMEvents::EEPROM_ACCESS_FAILED, &rec);
    }
}
