import unittest
import SpecUtils
from datetime import datetime

import os
from pathlib import Path


class TestSpecUtilsBasic(unittest.TestCase):
    def setUp(self):
        self.spec = SpecUtils.SpecFile()

    def test_load_file(self):
        script_dir = Path(__file__).parent.resolve()
        file_path = os.path.join(script_dir, "..", "examples", "passthrough.n42")
        self.spec.loadFile(file_path, SpecUtils.ParserType.Auto)
        self.assertTrue(self.spec.numMeasurements() > 0)

    def test_create_measurement(self):
        # Create and configure a new measurement
        meas = SpecUtils.Measurement()
        
        # Test setting basic properties
        new_live_time = 10
        new_real_time = 15
        new_gamma_counts = [0, 1.1, 2, 3, 4, 5.9, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1]
        meas.setGammaCounts(new_gamma_counts, new_live_time, new_real_time)
    
        # Assert the values were set correctly
        self.assertEqual( meas.liveTime(), new_live_time )
        self.assertEqual( meas.realTime(), new_real_time )
        self.assertEqual( len(list(meas.gammaCounts())), len(new_gamma_counts) )
        # We cant directly compare the arrays, since the original numbers are doubles
        # and SpecUtils uses floats in Python
        for i in range(len(new_gamma_counts)):
            self.assertAlmostEqual(list(meas.gammaCounts())[i], new_gamma_counts[i], places=5)

    def test_energy_calibration(self):
        meas = SpecUtils.Measurement()
        
        # Set up energy calibration
        energy_cal_coefficients = [0, 3000]
        deviation_pairs = [(100, -10), (1460, 15), (3000, 0)]
        new_gamma_counts = [0, 1.1, 2, 3, 4, 5.9, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1]
        num_channels = len(new_gamma_counts)
        new_live_time = 10
        new_real_time = 15
        new_gamma_counts = [0, 1.1, 2, 3, 4, 5.9, 6, 7, 8, 9, 8, 7, 6, 5, 4, 3, 2, 1]
        meas.setGammaCounts(new_gamma_counts, new_live_time, new_real_time)
    

        energy_cal = SpecUtils.EnergyCalibration.fromFullRangeFraction(
            num_channels, 
            energy_cal_coefficients, 
            deviation_pairs
        )
        
        meas.setEnergyCalibration(energy_cal)
        
        # Test channel to energy conversion
        test_channel = 9  # middle channel
        lower_energy = meas.gammaChannelLower(test_channel)
        upper_energy = meas.gammaChannelUpper(test_channel)
        self.assertLess(lower_energy, upper_energy)

    def test_file_operations(self):
        info = SpecUtils.SpecFile()
        
        # Test file loading
        with self.assertRaises(RuntimeError):
            # Should raise error for non-existent file
            info.loadFile("nonexistent_file.n42", SpecUtils.ParserType.Auto)

    def test_measurement_metadata(self):
        meas = SpecUtils.Measurement()
        
        # Test setting metadata
        test_title = "Test Measurement"
        test_time = datetime.fromisoformat('2022-08-26T00:05:23')
        test_remarks = ['Test Remark 1', 'Test Remark 2']
        
        meas.setTitle(test_title)
        meas.setStartTime(test_time)
        meas.setRemarks(test_remarks)
        meas.setSourceType(SpecUtils.SourceType.Foreground)
        
        # Verify metadata
        self.assertEqual( meas.title(), test_title )
        self.assertEqual( meas.startTime(), test_time )
        self.assertEqual( meas.remarks(), test_remarks )
        self.assertEqual( meas.sourceType(), SpecUtils.SourceType.Foreground )

    def test_position_setting(self):
        meas = SpecUtils.Measurement()
        
        test_lat = 37.6762183189832
        test_lon = -121.70622613299014
        test_time = datetime.fromisoformat('2022-08-26T00:05:23')
        
        meas.setPosition(
            Latitude=test_lat,
            Longitude=test_lon,
            PositionTime=test_time
        )
        
        self.assertEqual( meas.latitude(), test_lat )
        self.assertEqual( meas.longitude(), test_lon )
        self.assertEqual( meas.positionTime(), test_time )

if __name__ == '__main__':
    unittest.main()