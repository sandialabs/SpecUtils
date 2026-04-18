import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.ParserType;
import gov.sandia.specutils.Measurement;
import gov.sandia.specutils.FloatVector;
import gov.sandia.specutils.SaveSpectrumAsType;
import gov.sandia.specutils.SpecUtilsSwig;
import gov.sandia.specutils.SWIGTYPE_p_std__ostream;

/**
 * Simple test to verify the SpecUtils Java SWIG bindings load and work.
 *
 * Usage (from the build directory):
 *   javac -classpath .:SpecUtils.jar TestSpecUtils.java
 *   java -Djava.library.path="." -classpath .:SpecUtils.jar TestSpecUtils [optional_spectrum_file]
 */
public class TestSpecUtils {

    static {
        try {
            System.loadLibrary("SpecUtilsJni");
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Failed to load SpecUtilsJni: " + e.toString());
            System.exit(1);
        }
    }

    static int testsPassed = 0;
    static int testsFailed = 0;

    static void check(String name, boolean condition) {
        if (condition) {
            testsPassed++;
            System.out.println("  PASS: " + name);
        } else {
            testsFailed++;
            System.out.println("  FAIL: " + name);
        }
    }

    public static void main(String[] args) {
        System.out.println("=== SpecUtils Java Bindings Test ===\n");

        // Test 1: Create SpecFile object
        System.out.println("Test: Create SpecFile");
        SpecFile specFile = new SpecFile();
        check("SpecFile created", specFile != null);
        check("No measurements initially", specFile.num_measurements() == 0);

        // Test 2: Load a file if provided
        String testFile = null;
        if (args.length > 0) {
            testFile = args[0];
        } else {
            // Try to find a test file relative to common locations
            String[] candidates = {
                "../../../bindings/python/examples/passthrough.n42",
                "../../bindings/python/examples/passthrough.n42",
                "../bindings/python/examples/passthrough.n42",
            };
            for (String c : candidates) {
                java.io.File f = new java.io.File(c);
                if (f.exists()) {
                    testFile = c;
                    break;
                }
            }
        }

        if (testFile != null) {
            System.out.println("\nTest: Load spectrum file (" + testFile + ")");
            boolean loaded = specFile.load_file(testFile, ParserType.Auto, "");
            check("File loaded successfully", loaded);

            if (loaded) {
                long numMeas = specFile.num_measurements();
                check("Has measurements", numMeas > 0);
                System.out.println("    Number of measurements: " + numMeas);

                String manufacturer = specFile.manufacturer();
                System.out.println("    Manufacturer: " + manufacturer);

                String model = specFile.instrument_model();
                System.out.println("    Model: " + model);

                // Test: Access first measurement
                if (numMeas > 0) {
                    System.out.println("\nTest: Access measurement data");
                    Measurement meas = specFile.measurement((int) 0);
                    check("Got measurement", meas != null);

                    if (meas != null) {
                        float realTime = meas.real_time();
                        float liveTime = meas.live_time();
                        check("Real time > 0", realTime > 0);
                        check("Live time > 0", liveTime > 0);
                        System.out.println("    Real time: " + realTime + "s");
                        System.out.println("    Live time: " + liveTime + "s");

                        FloatVector counts = meas.gamma_counts();
                        if (counts != null && counts.size() > 0) {
                            check("Has gamma counts", counts.size() > 0);
                            System.out.println("    Number of channels: " + counts.size());

                            // Sum all counts
                            double totalCounts = 0;
                            for (int i = 0; i < counts.size(); i++) {
                                totalCounts += counts.get(i);
                            }
                            check("Total counts > 0", totalCounts > 0);
                            System.out.println("    Total counts: " + totalCounts);
                        }

                        // Test energy calibration model
                        System.out.println("\nTest: Energy calibration");
                        check("Has energy calibration model",
                              meas.energy_calibration_model() != null);
                    }
                }

                // Test: Write to file using ostream helper
                System.out.println("\nTest: Write spectrum file");
                SWIGTYPE_p_std__ostream outStrm = SpecUtilsSwig.openFile("test_output.pcf");
                specFile.write_pcf(outStrm);
                SpecUtilsSwig.closeFile(outStrm);
                java.io.File outFile = new java.io.File("test_output.pcf");
                check("Wrote PCF file", outFile.exists() && outFile.length() > 0);
                if (outFile.exists()) {
                    outFile.delete();
                }
            }
        } else {
            System.out.println("\nSkipping file I/O tests (no test file found)");
        }

        // Test: FloatVector operations
        System.out.println("\nTest: FloatVector operations");
        FloatVector fv = new FloatVector();
        fv.add(1.0f);
        fv.add(2.0f);
        fv.add(3.0f);
        check("FloatVector size", fv.size() == 3);
        check("FloatVector get(0)", fv.get(0) == 1.0f);
        check("FloatVector get(2)", fv.get(2) == 3.0f);

        // Summary
        System.out.println("\n=== Results ===");
        System.out.println("Passed: " + testsPassed);
        System.out.println("Failed: " + testsFailed);

        System.exit(testsFailed > 0 ? 1 : 0);
    }
}
