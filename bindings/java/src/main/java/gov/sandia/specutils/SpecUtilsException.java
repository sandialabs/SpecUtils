package gov.sandia.specutils;

/**
 * Thrown when a native SpecUtils call fails — for example, when a file
 * cannot be parsed, when a requested write format fails, or when an energy
 * calibration cannot be applied.
 *
 * Unchecked so callers are not forced to wrap every call in try/catch,
 * matching the lightweight feel of the C# binding.
 */
public class SpecUtilsException extends RuntimeException {
    private static final long serialVersionUID = 1L;

    public SpecUtilsException(String message) {
        super(message);
    }

    public SpecUtilsException(String message, Throwable cause) {
        super(message, cause);
    }
}
