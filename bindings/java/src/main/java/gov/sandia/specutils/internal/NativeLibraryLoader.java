package gov.sandia.specutils.internal;

import java.io.File;

/**
 * Loads the {@code SpecUtilsJava} native library that contains the JNI glue.
 *
 * Resolution order:
 * <ol>
 *   <li>If the system property {@code specutils.nativeLibrary} is set, load
 *       that file path via {@link System#load(String)}. Useful when the
 *       shared library lives somewhere that is not on
 *       {@code java.library.path}.</li>
 *   <li>Otherwise, {@link System#loadLibrary(String)} with the name
 *       {@code SpecUtilsJava}. The JVM maps that to
 *       {@code libSpecUtilsJava.so}, {@code libSpecUtilsJava.jnilib}, or
 *       {@code SpecUtilsJava.dll} as appropriate and searches
 *       {@code java.library.path}.</li>
 * </ol>
 *
 * On failure the original {@link UnsatisfiedLinkError} is rethrown with a
 * hint about setting {@code -Djava.library.path} so the user can see why.
 *
 * This class is package-internal; callers should just reference any wrapper
 * class (SpecFile, Measurement, EnergyCalibration) and the load happens
 * transparently on first use.
 */
public final class NativeLibraryLoader {

    private NativeLibraryLoader() { }

    private static final String PROP_EXPLICIT_PATH = "specutils.nativeLibrary";
    private static final String LIBRARY_NAME = "SpecUtilsJava";

    private static volatile boolean loaded = false;

    /**
     * Ensures the native library has been loaded. Idempotent.
     */
    public static synchronized void ensureLoaded() {
        if (loaded) {
            return;
        }
        String explicit = System.getProperty(PROP_EXPLICIT_PATH);
        try {
            if (explicit != null && !explicit.isEmpty()) {
                File f = new File(explicit);
                if (!f.isFile()) {
                    throw new UnsatisfiedLinkError(
                        "System property " + PROP_EXPLICIT_PATH
                        + " points to '" + explicit + "', which does not exist.");
                }
                System.load(f.getAbsolutePath());
            } else {
                System.loadLibrary(LIBRARY_NAME);
            }
        } catch (UnsatisfiedLinkError e) {
            throw new UnsatisfiedLinkError(
                "Failed to load native library '" + LIBRARY_NAME + "'. "
                + "Either place libSpecUtilsJava.{so,jnilib,dll} on the JVM's "
                + "java.library.path (use -Djava.library.path=...), or set "
                + "-D" + PROP_EXPLICIT_PATH + "=/path/to/library. "
                + "Original error: " + e.getMessage());
        }
        loaded = true;
    }
}
