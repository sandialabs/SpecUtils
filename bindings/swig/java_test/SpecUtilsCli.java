import gov.sandia.specutils.SpecFile;
import gov.sandia.specutils.ParserType;
import gov.sandia.specutils.SaveSpectrumAsType;
import gov.sandia.specutils.Measurement;
import gov.sandia.specutils.FloatVector;
import gov.sandia.specutils.StringVector;
import gov.sandia.specutils.IntVector;

/**
 * Command-line tool to inspect gamma spectrum files and optionally convert to N42-2012 format.
 *
 * Usage:
 *   java -Djava.library.path=. -cp .:SpecUtils.jar SpecUtilsCli <input_file> [output.n42]
 *
 * If an output path is given and the file does not already exist, writes an N42-2012 file.
 */
public class SpecUtilsCli {

    static {
        try {
            System.loadLibrary("SpecUtilsJni");
        } catch (UnsatisfiedLinkError e) {
            System.err.println("Failed to load native library: " + e.getMessage());
            System.exit(1);
        }
    }

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

        SpecFile specFile = new SpecFile();
        if (!specFile.load_file(inputPath, ParserType.Auto, "")) {
            System.err.println("Error: Failed to parse spectrum file: " + inputPath);
            System.exit(1);
        }

        System.out.println("File: " + inputPath);
        System.out.println();

        // Instrument info
        String manufacturer = specFile.manufacturer();
        String model = specFile.instrument_model();
        String id = specFile.instrument_id();
        if (!manufacturer.isEmpty() || !model.isEmpty())
            System.out.println("Instrument:    " + manufacturer + (model.isEmpty() ? "" : " " + model));
        if (!id.isEmpty())
            System.out.println("Serial:        " + id);

        String uuid = specFile.uuid();
        if (!uuid.isEmpty())
            System.out.println("UUID:          " + uuid);

        // Detector info
        StringVector detNames = specFile.detector_names();
        if (detNames.size() > 0) {
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < detNames.size(); i++) {
                if (i > 0) sb.append(", ");
                sb.append(detNames.get(i));
            }
            System.out.println("Detectors:     " + sb.toString());
        }

        // Measurement summary
        long numMeas = specFile.num_measurements();
        System.out.println("Measurements:  " + numMeas);

        double totalGamma = specFile.gamma_count_sum();
        System.out.println("Total gamma:   " + String.format("%.0f", totalGamma) + " counts");

        // Location
        double lat = specFile.mean_latitude();
        double lon = specFile.mean_longitude();
        if (!Double.isNaN(lat) && !Double.isNaN(lon)
            && Math.abs(lat) <= 90 && Math.abs(lon) <= 180) {
            System.out.println("Location:      " + String.format("%.6f, %.6f", lat, lon));
        }

        // Remarks
        StringVector remarks = specFile.remarks();
        if (remarks.size() > 0) {
            System.out.println("Remarks:");
            for (int i = 0; i < remarks.size(); i++) {
                String r = remarks.get(i).trim();
                if (!r.isEmpty())
                    System.out.println("  - " + r);
            }
        }

        // Per-detector summary for first few measurements
        System.out.println();
        int showCount = (int) Math.min(numMeas, 5);
        System.out.println("First " + showCount + " measurement(s):");
        for (int i = 0; i < showCount; i++) {
            Measurement m = specFile.measurement(i);
            if (m == null) continue;

            float rt = m.real_time();
            float lt = m.live_time();
            double counts = m.gamma_count_sum();
            long nChan = 0;
            FloatVector gc = m.gamma_counts();
            if (gc != null) nChan = gc.size();

            System.out.println(String.format(
                "  [%d] det=\"%s\" sample=%d  channels=%d  real=%.2fs  live=%.2fs  counts=%.0f",
                i, m.detector_name(), m.sample_number(), nChan, rt, lt, counts
            ));
        }
        if (numMeas > showCount) {
            System.out.println("  ... and " + (numMeas - showCount) + " more");
        }

        // Write N42-2012 if requested
        if (outputPath != null) {
            java.io.File outFile = new java.io.File(outputPath);
            if (outFile.exists()) {
                System.err.println("\nOutput file already exists, not overwriting: " + outputPath);
                System.exit(1);
            }

            try {
                specFile.write_to_file(outputPath, SaveSpectrumAsType.N42_2012);
                System.out.println("\nWrote N42-2012: " + outputPath);
            } catch (Exception e) {
                System.err.println("\nError writing N42 file: " + e.getMessage());
                System.exit(1);
            }
        }
    }
}
