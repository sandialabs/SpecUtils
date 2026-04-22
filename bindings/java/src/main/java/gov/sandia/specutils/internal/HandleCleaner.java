package gov.sandia.specutils.internal;

import java.lang.ref.PhantomReference;
import java.lang.ref.Reference;
import java.lang.ref.ReferenceQueue;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Safety-net cleanup for native handles.
 *
 * Users are expected to call {@code close()} on wrapper classes (SpecFile,
 * Measurement, EnergyCalibration) via try-with-resources. This class exists
 * only to clean up the native memory of wrappers that become unreachable
 * without {@code close()} being called — preventing leaks from buggy user
 * code without punishing well-written code.
 *
 * <h3>Why not java.lang.ref.Cleaner?</h3>
 *
 * {@link java.lang.ref.Cleaner} (since Java 9) does exactly what this class
 * does, and is what we'd use if the minimum supported runtime were Java 11+.
 * The project currently targets Java 8 (see
 * {@code bindings/java/CMakeLists.txt} — {@code --release 8}), so we roll
 * our own using {@link PhantomReference} + a daemon thread. Java 8 APIs only.
 *
 * <h3>Migration to Cleaner</h3>
 *
 * When Java 11+ becomes the floor, this class can be replaced with a thin
 * wrapper around {@code java.lang.ref.Cleaner}:
 *
 * <pre>{@code
 *   private static final Cleaner CLEANER = Cleaner.create();
 *   public static Registration register(Object owner, Runnable action) {
 *       Cleaner.Cleanable c = CLEANER.register(owner, action);
 *       return new Registration() {
 *           public void deregister() { c.clean(); }
 *       };
 *   }
 * }</pre>
 *
 * The public API here ({@link #register(Object, Runnable)} returning a
 * {@link Registration}) is already shaped to allow that swap with no changes
 * in the callers.
 */
public final class HandleCleaner {

    private HandleCleaner() { }

    /**
     * A handle to a registered cleanup action. Call {@link #deregister()}
     * from an explicit {@code close()} so the action does not run a second
     * time after the wrapper is garbage-collected.
     */
    public interface Registration {
        /**
         * Runs the cleanup action immediately (if not already run) and
         * removes it from the safety-net queue. Idempotent — calling twice
         * is a no-op.
         */
        void cleanAndDeregister();

        /**
         * Removes the cleanup action from the safety-net queue WITHOUT
         * running it. Use this when ownership of the handle has been
         * transferred to another object (e.g. SpecFile.addMeasurement takes
         * ownership of a Measurement's handle).
         */
        void deregister();
    }

    private static final ReferenceQueue<Object> QUEUE = new ReferenceQueue<>();

    // Keep phantom references alive until the owner is unreachable; once the
    // owner is collected, the phantom is enqueued and we remove it here.
    private static final ConcurrentHashMap<CleanupPhantom, Boolean> REGISTERED =
            new ConcurrentHashMap<>();

    static {
        Thread t = new Thread(HandleCleaner::runCleanupLoop, "SpecUtils-HandleCleaner");
        t.setDaemon(true);
        t.start();
    }

    public static Registration register(Object owner, Runnable cleanupAction) {
        if (owner == null) {
            throw new NullPointerException("owner");
        }
        if (cleanupAction == null) {
            throw new NullPointerException("cleanupAction");
        }
        CleanupPhantom phantom = new CleanupPhantom(owner, cleanupAction);
        REGISTERED.put(phantom, Boolean.TRUE);
        return phantom;
    }

    private static void runCleanupLoop() {
        while (true) {
            try {
                Reference<?> ref = QUEUE.remove();
                if (ref instanceof CleanupPhantom) {
                    ((CleanupPhantom) ref).runFromQueue();
                }
            } catch (InterruptedException e) {
                // daemon thread — honor interrupt by exiting
                return;
            } catch (Throwable t) {
                // Never let an exception kill the cleanup thread — the next
                // leaked handle still needs to be freed.
                System.err.println("SpecUtils HandleCleaner: cleanup action threw " + t);
            }
        }
    }

    private static final class CleanupPhantom
            extends PhantomReference<Object>
            implements Registration {

        private volatile Runnable action;

        CleanupPhantom(Object owner, Runnable action) {
            super(owner, QUEUE);
            this.action = action;
        }

        @Override
        public void cleanAndDeregister() {
            Runnable a = takeAction();
            REGISTERED.remove(this);
            if (a != null) {
                a.run();
            }
        }

        @Override
        public void deregister() {
            takeAction();
            REGISTERED.remove(this);
        }

        void runFromQueue() {
            Runnable a = takeAction();
            REGISTERED.remove(this);
            if (a != null) {
                a.run();
            }
        }

        private synchronized Runnable takeAction() {
            Runnable a = this.action;
            this.action = null;
            return a;
        }
    }
}
