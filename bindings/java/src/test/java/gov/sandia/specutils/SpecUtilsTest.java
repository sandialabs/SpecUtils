package gov.sandia.specutils;

// Explicit imports (redundant in-package, but required if this file is
// stripped of its `package` line and compiled in the default package,
// which is what the CI step does to mirror the SWIG binding's layout).
import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.Measurement;
import gov.sandia.specutils.EnergyCalibration;
import gov.sandia.specutils.EnergyCalType;
import gov.sandia.specutils.SaveSpectrumAsType;
import gov.sandia.specutils.SpecUtilsException;

/**
 * Smoke test for the hand-written JNI bindings. Mirrors
 * {@code bindings/swig/java_test/TestSpecUtils.java} but uses the new API
 * (try-with-resources, exceptions instead of bool returns, camelCase
 * method names, primitive float[] instead of FloatVector).
 *
 * Usage (from build dir):
 *   javac -classpath .:SpecUtils.jar SpecUtilsTest.java
 *   java -Djava.library.path="." -classpath .:SpecUtils.jar SpecUtilsTest [spectrum-file]
 */
public class SpecUtilsTest {

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
        System.out.println("=== SpecUtils JNI Bindings Test ===\n");

        String testFile = findTestFile(args);

        try (SpecFile specFile = new SpecFile()) {
            check("SpecFile created", specFile != null);
            check("No measurements initially", specFile.numberMeasurements() == 0);

            if (testFile == null) {
                System.out.println("\nSkipping file I/O tests (no test file found)");
            } else {
                System.out.println("\nTest: Load spectrum file (" + testFile + ")");
                try {
                    specFile.loadFile(testFile);
                    check("File loaded successfully", true);
                } catch (SpecUtilsException e) {
                    check("File loaded successfully", false);
                    System.err.println("    " + e.getMessage());
                    System.exit(1);
                }

                int numMeas = specFile.numberMeasurements();
                check("Has measurements", numMeas > 0);
                System.out.println("    Number of measurements: " + numMeas);
                System.out.println("    Manufacturer: " + specFile.manufacturer());
                System.out.println("    Model: " + specFile.instrumentModel());

                if (numMeas > 0) {
                    System.out.println("\nTest: Access measurement data");
                    Measurement meas = specFile.measurement(0);
                    check("Got measurement", meas != null);
                    if (meas != null) {
                        float realTime = meas.realTime();
                        float liveTime = meas.liveTime();
                        check("Real time > 0", realTime > 0);
                        check("Live time > 0", liveTime > 0);
                        System.out.println("    Real time: " + realTime + "s");
                        System.out.println("    Live time: " + liveTime + "s");

                        float[] counts = meas.gammaChannelCounts();
                        check("Has gamma counts", counts.length > 0);
                        System.out.println("    Number of channels: " + counts.length);

                        double totalCounts = 0;
                        for (float c : counts) totalCounts += c;
                        check("Total counts > 0", totalCounts > 0);
                        System.out.println("    Total counts: " + totalCounts);

                        System.out.println("\nTest: Energy calibration");
                        EnergyCalibration cal = meas.energyCalibration();
                        check("Has energy calibration", cal != null);
                        if (cal != null) {
                            EnergyCalType t = cal.type();
                            check("Cal type not invalid",
                                t != EnergyCalType.InvalidEquationType);
                            System.out.println("    Cal type: " + t);
                            // borrowed cal — close() is a no-op but still good hygiene
                            cal.close();
                        }
                    }
                }

                System.out.println("\nTest: Write spectrum file (PCF round-trip)");
                java.io.File outFile = new java.io.File("test_output.pcf");
                try {
                    specFile.writeToFile(outFile.getPath(), SaveSpectrumAsType.Pcf);
                    check("Wrote PCF file", outFile.exists() && outFile.length() > 0);

                    try (SpecFile reloaded = new SpecFile()) {
                        reloaded.loadFile(outFile.getPath());
                        check("Reloaded PCF has measurements",
                            reloaded.numberMeasurements() > 0);
                    }
                } catch (SpecUtilsException e) {
                    check("Wrote PCF file", false);
                    System.err.println("    " + e.getMessage());
                } finally {
                    if (outFile.exists()) outFile.delete();
                }
            }
        }

        // Exercise error handling: loadFile of missing path should throw
        System.out.println("\nTest: loadFile throws on missing path");
        boolean threw = false;
        try (SpecFile sf = new SpecFile()) {
            sf.loadFile("does/not/exist.n42");
        } catch (SpecUtilsException e) {
            threw = true;
        }
        check("loadFile threw on missing path", threw);

        System.out.println("\n=== Results ===");
        System.out.println("Passed: " + testsPassed);
        System.out.println("Failed: " + testsFailed);
        System.exit(testsFailed > 0 ? 1 : 0);
    }

    static String findTestFile(String[] args) {
        if (args.length > 0) return args[0];
        String[] candidates = {
            "../../../bindings/python/examples/passthrough.n42",
            "../../bindings/python/examples/passthrough.n42",
            "../bindings/python/examples/passthrough.n42",
        };
        for (String c : candidates) {
            if (new java.io.File(c).exists()) return c;
        }
        return null;
    }
}
