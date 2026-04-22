package gov.sandia.specutils;

// Explicit imports so this file also compiles when stripped of its
// `package` line and placed in the default package (as the CI step does).
import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.Measurement;
import gov.sandia.specutils.SaveSpectrumAsType;
import gov.sandia.specutils.SpecUtilsException;

/**
 * Command-line tool to inspect gamma spectrum files and optionally convert
 * to N42-2012 format — the JNI-binding version of the SWIG
 * {@code SpecUtilsCli.java} tool.
 *
 * Usage:
 *   java -Djava.library.path=. -cp .:SpecUtils.jar \
 *        gov.sandia.specutils.SpecUtilsCli &lt;input&gt; [output.n42]
 */
public class SpecUtilsCli {

    public static void main(String[] args) {
        if (args.length < 1) {
            System.err.println("Usage: SpecUtilsCli <input_spectrum_file> [output.n42]");
            System.exit(1);
        }

        String inputPath = args[0];
        String outputPath = (args.length >= 2) ? args[1] : null;

        java.io.File inputFile = new java.io.File(inputPath);
        if (!inputFile.exists()) {
            System.err.println("Error: File not found: " + inputPath);
            System.exit(1);
        }

        try (SpecFile specFile = new SpecFile()) {
            try {
                specFile.loadFile(inputPath);
            } catch (SpecUtilsException e) {
                System.err.println("Error: Failed to parse spectrum file: " + inputPath);
                System.err.println("  " + e.getMessage());
                System.exit(1);
            }

            System.out.println("File: " + inputPath);
            System.out.println();

            String manufacturer = nullToEmpty(specFile.manufacturer());
            String model = nullToEmpty(specFile.instrumentModel());
            String id = nullToEmpty(specFile.instrumentId());
            if (!manufacturer.isEmpty() || !model.isEmpty()) {
                System.out.println("Instrument:    " + manufacturer
                    + (model.isEmpty() ? "" : " " + model));
            }
            if (!id.isEmpty()) System.out.println("Serial:        " + id);

            String uuid = nullToEmpty(specFile.uuid());
            if (!uuid.isEmpty()) System.out.println("UUID:          " + uuid);

            int numDet = specFile.numberDetectors();
            if (numDet > 0) {
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < numDet; i++) {
                    if (i > 0) sb.append(", ");
                    sb.append(nullToEmpty(specFile.detectorName(i)));
                }
                System.out.println("Detectors:     " + sb);
            }

            int numMeas = specFile.numberMeasurements();
            System.out.println("Measurements:  " + numMeas);
            System.out.println("Total gamma:   "
                + String.format("%.0f", specFile.gammaCountSum()) + " counts");

            if (specFile.hasGpsInfo()) {
                double lat = specFile.meanLatitude();
                double lon = specFile.meanLongitude();
                System.out.println("Location:      " + String.format("%.6f, %.6f", lat, lon));
            }

            int nr = specFile.numberRemarks();
            if (nr > 0) {
                System.out.println("Remarks:");
                for (int i = 0; i < nr; i++) {
                    String r = nullToEmpty(specFile.remark(i)).trim();
                    if (!r.isEmpty()) System.out.println("  - " + r);
                }
            }

            System.out.println();
            int showCount = Math.min(numMeas, 5);
            System.out.println("First " + showCount + " measurement(s):");
            for (int i = 0; i < showCount; i++) {
                Measurement m = specFile.measurement(i);
                if (m == null) continue;
                int nChan = m.numberGammaChannels();
                System.out.println(String.format(
                    "  [%d] det=\"%s\" sample=%d  channels=%d  real=%.2fs  live=%.2fs  counts=%.0f",
                    i, nullToEmpty(m.detectorName()), m.sampleNumber(), nChan,
                    m.realTime(), m.liveTime(), m.gammaCountSum()
                ));
            }
            if (numMeas > showCount) {
                System.out.println("  ... and " + (numMeas - showCount) + " more");
            }

            if (outputPath != null) {
                java.io.File outFile = new java.io.File(outputPath);
                if (outFile.exists()) {
                    System.err.println("\nOutput file already exists, not overwriting: "
                        + outputPath);
                    System.exit(1);
                }
                try {
                    specFile.writeToFile(outputPath, SaveSpectrumAsType.N42_2012);
                    System.out.println("\nWrote N42-2012: " + outputPath);
                } catch (SpecUtilsException e) {
                    System.err.println("\nError writing N42 file: " + e.getMessage());
                    System.exit(1);
                }
            }
        }
    }

    private static String nullToEmpty(String s) { return s == null ? "" : s; }
}
